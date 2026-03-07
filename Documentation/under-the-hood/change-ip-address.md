# Change IP address

When performing a [live migration](live-migration.md) of a process from one host to another, a common question is how to handle a different IP address on the destination host. While the recommended approach is to use containers, moving a service to a different IP address may sometimes be necessary. This article describes how to achieve this.

> **Note:** This feature is not yet implemented in CRIU: [https://github.com/checkpoint-restore/criu/issues/211](https://github.com/checkpoint-restore/criu/issues/211)

## Problem

Simply changing the IP address and expecting things to work as before is not possible—not because of CRIU constraints, but due to how the [TCP protocol](tcp-connection.md) operates. Packet flow cannot continue if an IP address changes; the client would simply ignore such packets.

When migrating a server to a host with a different IP address, three things must be considered.

### Listening sockets

If your server is bound to `0.0.0.0` (`INADDR_ANY`), then migration will "just work" because no IP address mismatch will occur. If your server is bound to a specific device, you will need to change the binding IP address. Currently, this can be done by editing the [images](images.md); specifically, all `PF_INET` sockets are stored in the `files.img` image, and [CRIT](crit.md) can be used to modify them.

### In-flight connections

These are connections that have been `connect()`ed but not yet `accept()`ed. The `--skip-in-flight` option allows CRIU to ignore these connections.

### Established sockets

These are challenging because they have a specific IP address baked into their configuration. While it is technically possible to restore a socket with a different IP address (by modifying `inetsk.img` with [CRIT](crit.md)), the peer would not accept it. In the worst case, the connection would remain stuck until a TCP timeout occurs.

## Possible solution

If breaking these connections is acceptable, we need to enable CRIU to do so. There are two aspects to consider:

a) **Dumping sockets**: Since the connection is no longer needed, CRIU should be taught to skip these sockets. The code for dumping `PF_INET` sockets is in `criu/sk-inet.c`, and the code for dumping `IPPROTO_TCP` data is in `criu/sk-tcp.c`.

b) **Restoring sockets**: Leaving a "hole" where a connected socket once existed is problematic; the server would receive incorrect error codes from syscalls, and worse, the hole might be filled by another file (e.g., when the server calls `open`, `socket`, or `accept`), breaking the server's internal logic. Therefore, at restore time, a stub should be placed into the descriptor. I suggest addressing this at dump-time: instead of dumping the established socket, dump a socket that appears to be closed. The restoration code would then restore this "closed" socket into the proper place.
