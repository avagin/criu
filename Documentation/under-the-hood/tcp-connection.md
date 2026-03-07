# TCP Connection

This page describes how CRIU handles established TCP connections.

## TCP Repair Mode in the Kernel

The `TCP_REPAIR` socket option was added to the Linux kernel in version 3.5 to facilitate checkpoint/restore for TCP sockets.

When this option is enabled, the socket enters a special mode where actions performed on it do not trigger standard protocol behaviors. Instead, they directly transition the socket into the expected state as if the operation had successfully completed.

For example:
- Calling `connect()` on a socket in repair mode simply changes its state to `ESTABLISHED` with the specified peer address.
- Calling `bind()` forcibly binds the socket to the given address, ignoring potential conflicts.
- Calling `close()` silently terminates the socket without undergoing `FIN_WAIT`, `TIME_WAIT`, or other transient states.

### Sequences
To correctly restore a connection, CRIU must also restore the TCP sequence numbers. This is achieved using the `TCP_REPAIR_QUEUE` and `TCP_QUEUE_SEQ` options. `TCP_REPAIR_QUEUE` selects either the input or output queue for repair, and `TCP_QUEUE_SEQ` gets or sets the sequence number. Note that sequence numbers can only be set on a closed socket.

### Packets in Queue
While in repair mode, standard `recv` and `send` system calls can be used to peek or poke data directly from/to the selected queue. This allows CRIU to capture the state of outgoing and incoming packet queues. The `MSG_PEEK` flag is required for `recv()` calls.

### Options
Four primary options are negotiated during the TCP connection stage:
- `mss_clamp`: The maximum segment size the peer can accept.
- `snd_scale`: The window scaling factor.
- `sack`: Whether selective acknowledgments are permitted.
- `tstamp`: Whether timestamps are supported.

These can be retrieved via `getsockopt()` and restored using the `TCP_REPAIR_OPTIONS` socket option.

## Timestamps
As per RFC 7323, the sender's timestamp clock provides monotonic, non-decreasing values for segments. The Linux kernel uses the `jiffies` counter as the TCP timestamp. CRIU uses the `TCP_TIMESTAMP` option to compensate for differences in `jiffies` counters when a connection is migrated to a different host. During a dump, CRIU records the current timestamp and, during restoration, sets it as the new starting point.

## Checkpoint and Restore of TCP Connections

Using these socket options, CRIU can read the socket state and restore it, allowing the protocol to resume the data sequence seamlessly.

Crucially, while the socket is closed between the dump and restoration, the connection must be "locked." This prevents packets from the peer from reaching the networking stack and causing the kernel to send a reset (RST). This is typically achieved using a netfilter rule to drop incoming packets from the peer. This rule must remain in place from the end of the dump until the restoration is complete. The locking method can be configured using the `--network-lock` option.

During restoration, the IP address used by the original connection must be available. While this is automatic if restoring on the same host, it must be manually handled for live migrations. Consequently, the `--tcp-established` option must be explicitly used to indicate the caller is aware of the transitional netfilter state.

If the target process resides in a network namespace, connection locking is handled via `network-lock` and `network-unlock` [action scripts](action-scripts.md), typically by bringing down the respective `veth` pair.

## States

### TCP_SYN_SENT
To restore this state, CRIU restores the socket and disables repair mode before calling `connect()`. The kernel then sends a single `SYN` packet with the original sequence number and transitions the socket to the `TCP_SYN_SENT` state.

### Half-Closed Sockets
A socket is considered half-closed if it has sent or received a `FIN` packet (states include `TCP_FIN_WAIT1`, `TCP_FIN_WAIT2`, `TCP_CLOSING`, `TCP_LAST_ACK`, and `TCP_CLOSE_WAIT`). To restore these, CRIU first restores the socket to the `TCP_ESTABLISHED` state. If the socket had sent a `FIN`, CRIU calls `shutdown(SHUT_WR)`. If it had received a `FIN`, CRIU sends a "fake" `FIN` packet. For example, to restore `TCP_FIN_WAIT2`, CRIU calls `shutdown(SHUT_WR)` and then processes a fake acknowledgment for the `FIN`.

## See also
- [Simple TCP pair](simple-tcp-pair.md)
- [TCP repair TODO](tcp-repair-todo.md)
- [Dropping the connection](cliopt--tcp-close.md)

## External links
- http://lwn.net/Articles/495304/
