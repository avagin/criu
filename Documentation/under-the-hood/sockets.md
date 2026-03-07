# Sockets

## UNIX Sockets Initial Support

CRIU currently supports all types of UNIX sockets, both IPv4 and IPv6 UDP sockets, Netlink sockets, and TCP sockets in both Listen and [Established](tcp-connection.md) states.

During a checkpoint, CRIU uses the `sock_diag` engine to collect extended socket information, which the file dumping engine then uses to capture the socket state.

Restoring UNIX sockets is particularly complex. While restoring listening sockets is straightforward, connected sockets are restored using the following procedure:

1. One endpoint establishes a listening anonymous socket at the target descriptor.
1. The other endpoint creates a socket at its target descriptor.
1. The endpoint to be connected calls `connect()`. Since UNIX sockets do not block `connect()` until `accept()` is called, the process continues.
1. All listening sockets then call `accept()`, and the resulting file descriptor is `dup2`'d into the accepting end.

One limitation of this approach is that socket names may not be preserved. However, based on experience with the OpenVZ implementation, this is generally acceptable for most applications.
