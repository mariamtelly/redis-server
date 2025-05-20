#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>
#include <vector>
#include <map>
#include "hashtable.h"

#define container_of(ptr, T, member) \
    ((T*)((char*)ptr - offsetof(T, member)))

const size_t k_max_msg = 32 << 20;
const uint32_t k_max_args = 200 * 1000;

typedef std::vector<uint8_t> Buffer;

enum {
    RES_OK = 0,
    RES_ERR = 1,    // error
    RES_NX = 2,     // key not found
};

struct Conn {
    int fd = -1;
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;

    Buffer incoming; // data to be parsed
    Buffer outgoing; // generated responses
};

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void msg_errno(const char *msg) {
    fprintf(stderr, "[errno:%d] %s\n", errno, msg);
}

static void die(const char *msg) {
    fprintf(stderr, "[%d] %s\n", errno, msg);
    abort();
}

// Make the socket non-blocking
static void fd_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        die("fcntl error");
        return;
    }
    flags |= O_NONBLOCK;
    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) die("fcntl error");
}

static Conn* handle_accept(int fd) {
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
    if (connfd < 0) {
        msg_errno("accept() error");
        return NULL;
    }

    uint32_t ip = client_addr.sin_addr.s_addr;
    fprintf(stderr, "new client from %u.%u.%u.%u:%u\n",
        ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24,
        ntohs(client_addr.sin_port)
    );

    // Make the accepted connection (client) non-blocking
    fd_set_nonblock(connfd);

    // Create a new Conn structure
    Conn* conn = new Conn();
    conn->fd = connfd;
    conn->want_read = true;

    return conn;
}

static void buf_append(std::vector<uint8_t> &buf, const uint8_t* data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

static void buf_consume(std::vector<uint8_t> &buf, size_t n) {
    buf.erase(buf.begin(), buf.begin() + n);
}

static bool read_u32(const uint8_t* &cur, const uint8_t* end, uint32_t &out) {
    if(4 + cur > end) return false;
    memcpy(&out, cur, 4);
    cur += 4;
    return true;
}

static bool read_str(const uint8_t *&cur, const uint8_t *end, size_t n, std::string &out) {
    if (cur + n > end) return false;
    out.assign((const char*)cur, (const char*)cur + n);
    cur += n;
    return true;
}

static int32_t parse_req(const uint8_t* data, size_t size, std::vector<std::string> &out) {
    const uint8_t* end = data + size;
    uint32_t nstr = 0;
    if(!read_u32(data, end, nstr)) return -1;
    if(nstr > k_max_args) return -1;

    while(out.size() < nstr) {
        uint32_t len = 0;
        if(!read_u32(data, end, len)) return -1;
        out.push_back(std::string());
        if(!read_str(data, end, len, out.back())) return -1;
    }
    if(data != end) return -1;
    return 0;
}

enum {
    ERR_UNKNOWN = 1,
    ERR_TOO_BIG = 2,
};

enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

static void buf_append_u8(Buffer &buf, uint8_t data) {
    buf.push_back(data);
}

static void buf_append_u32(Buffer &buf, uint32_t data) {
    buf_append(buf, (const uint8_t*)&data, 4);
}

static void buf_append_i64(Buffer &buf, int64_t data) {
    buf_append(buf, (const uint8_t*)&data, 8);
}

static void buf_append_dbl(Buffer &buf, double data) {
    buf_append(buf, (const uint8_t*)&data, 8);
}

static void out_nil(Buffer &out) {
    buf_append_u8(out, TAG_NIL);
}

static void out_str(Buffer &out, const char* s, size_t size) {
    buf_append_u8(out, TAG_STR);
    buf_append_u32(out, (uint32_t)size);
    buf_append(out, (const uint8_t*)s, size);
}

static void out_int(Buffer &out, int64_t val) {
    buf_append_u8(out, TAG_INT);
    buf_append_i64(out, val);
}

static void out_dbl(Buffer &out, double val) {
    buf_append_u8(out, TAG_DBL);
    buf_append_dbl(out, val);
}

static void out_err(Buffer &out, uint32_t code, const std::string &msg) {
    buf_append_u8(out, TAG_ERR);
    buf_append_u32(out, code);
    buf_append_u32(out, (uint32_t)msg.size());
    buf_append(out, (const uint8_t *)msg.data(), msg.size());
}

static void out_arr(Buffer &out, uint32_t n) {
    buf_append_u8(out, TAG_ARR);
    buf_append_u32(out, n);
}

static struct {
    HMap db;
} g_data; 

struct Entry {
    struct HNode node;
    std::string key;
    std::string val;
};

static bool entry_eq(HNode* lhs, HNode* rhs) {
    struct Entry* le = container_of(lhs, struct Entry, node);
    struct Entry* re = container_of(rhs, struct Entry, node);
    return le->key == re->key;
}

static uint64_t str_hash(const uint8_t* data, size_t len) {
    uint32_t h = 0x811C9DC5;
    for(size_t i = 0; i < len; i++) h = (h + data[i]) * 0x01000193;
    return h;
}

static void do_get(std::vector<std::string> &cmd, Buffer &resp) {
    // An entry for the lookup
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t*)key.key.data(), key.key.size());

    // Lookup the key
    HNode* node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (!node) { 
        // If it does not exist, return NIL
        return out_nil(resp);
    }

    // We have found the key, we copy the value to be sent back
    const std::string &val = container_of(node, Entry, node)->val;
    assert(val.size() <= k_max_msg);
    return out_str(resp, val.data(), val.size());
}

static void do_set(std::vector<std::string> &cmd, Buffer &resp) {
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    
    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (node) {
        // Update the value if the key already exist
        container_of(node, Entry, node)->val.swap(cmd[2]);
    } else {
        // Otherwise, create a new entry and insert it into the table
        Entry *ent = new Entry();
        ent->key.swap(key.key);
        ent->node.hcode = key.node.hcode;
        ent->val.swap(cmd[2]);
        hm_insert(&g_data.db, &ent->node);
    }
    return out_nil(resp);
}

static void do_del(std::vector<std::string> &cmd, Buffer &resp) {
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    // Delete the entry
    HNode *node = hm_delete(&g_data.db, &key.node, &entry_eq);
    if (node) { // deallocate the pair
        delete container_of(node, Entry, node);
    }
    return out_int(resp, node ? 1 : 0);
}

static bool cb_keys(HNode* node, void* arg) {
    Buffer &out = *(Buffer *)arg;
    const std::string &key = container_of(node, Entry, node)->key;
    out_str(out, key.data(), key.size());
    return true;
}

static void do_keys(std::vector<std::string> &, Buffer &out) {
    out_arr(out, (uint32_t)hm_size(&g_data.db));
    hm_foreach(&g_data.db, &cb_keys, (void *)&out);
}

static void do_request(std::vector<std::string> &cmd, Buffer &resp) {
    if(cmd.size() == 2 && cmd[0] == "get") {
        do_get(cmd, resp);
    } else if(cmd.size() == 3 && cmd[0] == "set") {
        do_set(cmd, resp);
    } else if(cmd.size() == 2 && cmd[0] == "del") {
        return do_del(cmd, resp);
    } else if (cmd.size() == 1 && cmd[0] == "keys") {
        return do_keys(cmd, resp);
    } else out_err(resp, ERR_UNKNOWN, "unknown command."); // Unrecognized command
}

static void response_begin(Buffer &out, size_t * header) {
    *header = out.size();
    buf_append_u32(out, 0);
}

static size_t response_size(Buffer &out, size_t header) {
    return out.size() - header - 4;
}

static void response_end(Buffer &out, size_t header) {
    size_t msg_size = response_size(out, header);
    if(msg_size > k_max_msg) {
        out.resize(header + 4);
        out_err(out, ERR_TOO_BIG, std::string("response is too big"));
        msg_size = response_size(out, header);
    }
    uint32_t len = (uint32_t)msg_size;
    memcpy(&out[header], &len, 4);
}

static bool try_one_request(Conn* conn) {
    // We want at least 4 bytes for the length of the message 
    if(conn->incoming.size() < 4) return false;

    uint32_t len;
    memcpy(&len, conn->incoming.data(), 4);

    if (len > k_max_msg) { 
        msg("too long");
        conn->want_close = true;
        return false;
    }

    if (4 + len > conn->incoming.size()) return false;

    const uint8_t* request = &conn->incoming[4];
    std::vector<std::string> cmd;
    if(parse_req(request, len, cmd) < 0) {
        msg("bad request");
        conn->want_close = true;
        return false;
    }

    size_t header_pos = 0;
    response_begin(conn->outgoing, &header_pos);
    do_request(cmd, conn->outgoing);
    response_end(conn->outgoing, header_pos);

    // Consume the request once treated
    buf_consume(conn->incoming, 4 + len);
    return true;
}

static void handle_write(Conn* conn) {
    assert(conn->outgoing.size() > 0);
    ssize_t rv = write(conn->fd, &conn->outgoing[0], conn->outgoing.size());
    if (rv < 0 && errno == EAGAIN) return; // Not ready to write
    if (rv < 0) {
        msg_errno("write() error");
        conn->want_close = true;
        return;
    }

    buf_consume(conn->outgoing, (size_t)rv);

    if (conn->outgoing.size() == 0) {
        conn->want_read = true;
        conn->want_write = false;
    }
}

static void handle_read(Conn* conn) {
    // Do a non-blocking read of 64 Ko
    uint8_t buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv < 0 && errno == EAGAIN) return; // Not ready to read
    if (rv < 0) {
        msg_errno("read() error"); 
        conn->want_close = true;
        return;
    }

    if (rv == 0) {
        if(conn->incoming.size() == 0) msg("Client closed");
        else msg("unexpected EOF");
        conn->want_close = true;
        return;
    }

    // Add incoming request to conn::incoming and then try to handle it 
    buf_append(conn->incoming, buf, (size_t)rv);
    while(try_one_request(conn)) {}

    if (conn->outgoing.size() > 0) {
        conn->want_read = false;
        conn->want_write = true;
        handle_write(conn);
    }
}

int main() {
    // Create a TCP socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0) {
        die("socket()");
    }

    // Allow reusing the same address (avoid "address already in use" error)
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // Bind
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0);    // wildcard address 0.0.0.0
    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
    if (rv) die("bind()");

    // set the listen fd to nonblocking mode
    fd_set_nonblock(fd);

    // listen
    rv = listen(fd, SOMAXCONN);
    if (rv) die("listen()");

    // Prepare two vectors:
    // - conns maps file descriptors to active Conn* objects
    // - poll_args holds all file descriptors and their poll settings
    std::vector<Conn*> conns;
    std::vector<struct pollfd> poll_args;

    while (true) {
        // Reset the poll arguments vector for this loop iteration
        poll_args.clear();

        // Add the listening socket to the poll list (to detect new clients)
        struct pollfd pfd = { fd, POLLIN, 0 }; // listen for input (new connection)
        poll_args.push_back(pfd);

        // Add all active client connections
        for (Conn* conn : conns) {
            if (!conn) continue; // skip empty slots

            struct pollfd pfd = { conn->fd, POLLERR, 0 }; // monitor for errors
            if (conn->want_read)  pfd.events |= POLLIN;   // if the conn wants to read
            if (conn->want_write) pfd.events |= POLLOUT;  // if the conn wants to write
            poll_args.push_back(pfd);
        }

        // Block until an event occurs on any file descriptor
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
        
        // If interrupted by a signal, continue polling
        if (rv < 0 && errno == EINTR) continue;
        // If another poll error occurs, exit the loop
        if (rv < 0) die("poll");

        // Check if there is an incoming connection on the listening socket
        if (poll_args[0].revents) {
            if (Conn* conn = handle_accept(fd)) {
                // Resize the conns vector to fit the new file descriptor if needed
                if (conns.size() <= (size_t)conn->fd) conns.resize(conn->fd + 1);
                // Store the new connection object
                conns[conn->fd] = conn;
            }
        }

        // Iterate over all other sockets (client connections)
        for (size_t i = 1; i < poll_args.size(); ++i) {
            uint32_t ready = poll_args[i].revents;
            Conn* conn = conns[poll_args[i].fd];

            // If the socket is ready for reading, handle input
            if (ready & POLLIN) handle_read(conn);

            // If the socket is ready for writing, handle output
            if (ready & POLLOUT) handle_write(conn);

            // If there was an error or the connection should close
            if ((ready & POLLERR) || conn->want_close) {
                (void)close(conn->fd);         // Close the socket
                conns[conn->fd] = NULL;      // Clear from connection table
                delete conn;                   // Free memory
            }
        }
    }

    return 0;
}