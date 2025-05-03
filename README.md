# Redis Server From Scratch

## About This Project

This project is about building a **Redis server from scratch**, without using any existing Redis library or framework.  
Why? Because **if you can build a Redis server, you can build almost any software**. Redis touches two essential areas of computer science:

- **Network programming** (sockets, protocols, client/server architecture)
- **Data structures** (hash tables, linked lists, sets, etc.)

By recreating Redis, I aim to deeply understand these core concepts, not just at a theoretical level but through real, working code.

## Why Start From Scratch?

To quote physicist Richard Feynman:  
> "What I cannot create, I do not understand."

Building something from scratch forces you to truly grasp its inner workings — no shortcuts, no magic. It’s the ultimate learning tool.

## What I Am Learning

Through this project, I am gaining hands-on experience with:

- **Socket programming**: handling client connections, managing multiple clients concurrently, dealing with low-level I/O.
- **Redis Serialization Protocol (RESP)**: understanding and implementing a simple, efficient communication format.
- **Core data structures**: building robust implementations of strings, lists, hashes, and sets.
- **Command parsing and execution**: designing a modular, extensible way to handle different Redis commands.
- **Memory management and performance**: thinking about efficient memory usage and response times.
- **Testing and robustness**: ensuring stability under various usage patterns and failure modes.

## Project Goals

- Implement a working subset of Redis commands (`SET`, `GET`, `DEL`, `EXISTS`, `PING`, `ECHO`, etc.)
- Support multiple concurrent clients
- Respect the RESP protocol for communication
- Write clean, modular, and well-documented code
- Make it easy to extend with new commands or features
- Make it with non-blocking I/O using `poll`

## Future Improvements

- Add support for more advanced data types (sorted sets, pub/sub, etc.)
- Implement persistence (AOF/RDB-style storage)
- Add replication and clustering support


## How to Run

Instructions will be added as development progresses.
