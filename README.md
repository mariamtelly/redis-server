# Redis Server From Scratch

## 📚 About This Project

This project is a from-scratch reimplementation of a **Redis-like in-memory database server**, written in **C++**, using **no third-party Redis libraries or frameworks**.

Redis is a powerful system that combines **network programming**, **data structures**, **asynchronous concurrency**, and **high-performance design**. By rebuilding it, this project became a deep dive into:

- Low-level **system programming**,
- High-level **software architecture**,
- And practical, production-inspired **performance engineering**.

## 🧠 What I've Built

- 🔌 A **non-blocking, event-driven TCP server**, using `poll()` for IO multiplexing.
- 📡 A **binary protocol**, with custom serialization and deserialization of requests and responses.
- 🧠 An extensible **command execution engine** that supports several Redis-like commands.
- 📦 A hash-based **key-value store** (`SET`, `GET`, `DEL`, `EXISTS`, `PING`, `ECHO`).
- 🧮 A **sorted set data type (ZSet)** using an **AVL tree** (for ordered iteration / offset queries) and a **hashtable** (for fast lookups).
- 🕒 A **TTL (time-to-live)** mechanism with expiration timers via a **min-heap**.
- ⏳ **Idle connection timeouts** using a **doubly-linked list** based timer queue.
- 🧵 A **thread pool** to offload heavy operations (like large set destruction) without blocking the event loop.

## ⚙️ Commands Supported

| Command     | Description                                                |
|-------------|------------------------------------------------------------|
| `set key val`   | Set a key-value pair                                    |
| `get key`       | Retrieve a value by key                                 |
| `del key`       | Delete a key                                            |
| `pexpire key ms` | Set a time-to-live (in ms) for a key                    |
| `pttl key`       | Get remaining TTL in ms                                 |
| `zadd zset score member` | Insert or update a member in a sorted set     |
| `zrem zset member`       | Remove a member from a sorted set              |
| `zscore zset member`     | Get the score of a member                      |
| `zquery zset min prefix offset limit` | Query sorted set by range        |

> 🧪 All of these are tested using a Python test script with expected outputs.

## 🤖 Architecture Highlights

- **Hash table (open addressing)**: For fast key lookup.
- **AVL tree**: Maintains ordering in ZSets and supports efficient offset-based queries.
- **Min-heap**: Efficient TTL expiration with O(log N) updates and O(1) access to next expiry.
- **Thread pool**: Offload expensive clean-up tasks to background threads.
- **Event loop**: The heart of the server, coordinating IO and timers with zero blocking.

## 🔥 Why This Project?

> *"What I cannot create, I do not understand."* — Richard Feynman

By replicating a complex production tool like Redis, I challenged myself to:
- Understand low-level systems (file descriptors, polling, socket programming).
- Manipulate and design high-performance data structures.
- Tackle real-world concerns like scalability, latency, and idle timeouts.
- Implement concurrency via thread pools and inter-thread synchronization.

## 🎯 Project Goals (Achieved)

- ✅ Handle concurrent clients with `poll()` (non-blocking I/O)
- ✅ Implement core Redis-like commands (including sorted set logic)
- ✅ Build from-scratch hash tables, trees, heaps
- ✅ Implement TTL and idle expiration using timers
- ✅ Add a thread pool for async operations (like `UNLINK`)
- ✅ Achieve high extensibility and testability

## 🛠 How to Build and Run

1. **Build the server**
   ```bash
   g++ -std=c++11 server.cpp zset.cpp heap.cpp hashtable.cpp avl.cpp thread_pool.cpp -o server

2. **Build the client**
    ```bash
    g++ -std=c++11 client.cpp -o client

3. **Execute server**
    ```bash
    ./server

4. **Execute the python script**
    ```bash
    python3 cmds_test.py
