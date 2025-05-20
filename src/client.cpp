#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <string.h>
#include <vector>

const size_t k_max_msg = 32 << 20;

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static int32_t read_full(int fd, char *buf, size_t n) {
    // While there is data to read, read it
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            return -1;  
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n) {
    // While there is data to write, write it
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1;  
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}  

static int32_t send_req(int fd, const std::vector<std::string> &cmd) {
    /*
     * Si on veut faire set name Mariam
     * n = 3 sur 4o
     * 3 sur 4o + "set" sur 3o
     * 4 sur 4o + "name" sur 4o
     * 6 sur 4o + "Mariam" sur 6o
     * Total = 4 + (4+3) + (4+4) + (6+4) = 29o
     * Donc ce qu'on envoie, c'est 29o (sur 4o)  
     * donc 33o en tout et pour tout
     * 29 3 3 "set" 4 "name" 6 "Mariam"
    */
    uint32_t len = 4; 
    for(const std::string &s : cmd) len += 4 + s.size(); 
    if(len > k_max_msg) return -1;

    // char wbuf[4 + k_max_msg];
    std::vector<char> wbuf;
    wbuf.resize(4 + len);
    memcpy(wbuf.data(), &len, 4);
    uint32_t n = cmd.size();
    memcpy(wbuf.data() + 4, &n, 4);

    size_t cur = 8;
    for(const std::string &s : cmd) {
        uint32_t p = (uint32_t)s.size();
        memcpy(wbuf.data() + cur, &p, 4);
        memcpy(wbuf.data() + cur + 4, s.data(), (size_t)p);
        cur += 4 + (size_t)p;
    }
    return write_all(fd, wbuf.data(), 4 + len);
}

enum {
    TAG_NIL = 0,    
    TAG_ERR = 1,    
    TAG_STR = 2,    
    TAG_INT = 3,   
    TAG_DBL = 4,    
    TAG_ARR = 5,    
};

static int32_t print_response(const uint8_t* data, size_t size) {
    if (size < 1) {
        msg("bad response");
        return -1;
    }
    switch(data[0]) {
        case TAG_NIL:
            printf("(nil)\n");
            return 1;
        case TAG_ERR:
            if (size < 1 + 8) {
                msg("bad response");
                return -1;
            }
            {
                int32_t code = 0;
                uint32_t len = 0;
                memcpy(&code, &data[1], 4);
                memcpy(&len, &data[1 + 4], 4);
                if (size < 1 + 8 + len) {
                    msg("bad response");
                    return -1;
                }
                printf("(err) %d %.*s\n", code, len, &data[1 + 8]);
                return 1 + 8 + len;
            }
        case TAG_STR:
            if (size < 1 + 4) {
                msg("bad response");
                return -1;
            }
            {
                uint32_t len = 0;
                memcpy(&len, &data[1], 4);
                if (size < 1 + 4 + len) {
                    msg("bad response");
                    return -1;
                }
                printf("(str) %.*s\n", len, &data[1 + 4]);
                return 1 + 4 + len;
            }
        case TAG_INT:
            if (size < 1 + 8) {
                msg("bad response");
                return -1;
            }
            {
                int64_t val = 0;
                memcpy(&val, &data[1], 8);
                printf("(int) %lld\n", val);
                return 1 + 8;
            }
        case TAG_DBL:
            if (size < 1 + 8) {
                msg("bad response");
                return -1;
            }
            {
                double val = 0;
                memcpy(&val, &data[1], 8);
                printf("(dbl) %g\n", val);
                return 1 + 8;
            }
        case TAG_ARR:
            if (size < 1 + 4) {
                msg("bad response");
                return -1;
            }
            {
                uint32_t len = 0;
                memcpy(&len, &data[1], 4);
                printf("(arr) len=%u\n", len);
                size_t arr_bytes = 1 + 4;
                for (uint32_t i = 0; i < len; ++i) {
                    int32_t rv = print_response(&data[arr_bytes], size - arr_bytes);
                    if (rv < 0) {
                        return rv;
                    }
                    arr_bytes += (size_t)rv;
                }
                printf("(arr) end\n");
                return (int32_t)arr_bytes;
            }
        default:
            msg("bad response");
            return -1;
    }
}

static int32_t read_res(int fd) {
    // char rbuf[4 + k_max_msg + 1];
    std::vector<char> rbuf;
    rbuf.resize(4);
    errno = 0;
    int32_t err = read_full(fd, rbuf.data(), 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf.data(), 4);  // assume little endian
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }

    // reply body
    rbuf.resize(4 + len);
    err = read_full(fd, rbuf.data() + 4, len);
    if (err) {
        msg("read() error");
        return err;
    }

    // print the result
    int32_t rv = print_response((uint8_t *)(rbuf.data() + 4), len);
    if (rv > 0 && (uint32_t)rv != len) {
        msg("bad response");
        rv = -1;
    }
    return rv;
}

int main(int argc, char** argv) {
    printf("argc %d\n", argc);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);  // 127.0.0.1
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv) die("connect");

    std::vector<std::string> cmd;
    for(int i = 1; i < argc; ++i) {
        printf("i = %d\n", i);
        cmd.push_back(argv[i]);
        printf("cmd[%d] : %s\n", i - 1, argv[i]);
    }

    int32_t err = send_req(fd, cmd);
    if(err) goto L_DONE;

    err = read_res(fd);
    if(err) goto L_DONE;

L_DONE:
    close(fd);
    return 0;
}