# Pidfd

This article describes how we checkpoint/restore pidfds (process file descriptors).

## Checkpointing
All information that we require for restore of pidfds is available in this `/proc` entry: `/proc/$pid/fdinfo/$pidfd`

Since CRIU does not support nested pid namespaces, the correct `pid` to use during restore is the last `pid` (i.e., `pid` in the most deeply nested pid namespace) in the `/proc/$pid/fdinfo/$pidfd/NSpid` field.
So, we only dump that `pid`.

## Restoring `pidfd(s)` Pointing to an Alive Process
CRIU, while restoring, first creates all processes in the process tree and then starts opening file descriptors in each process.

So, If the pidfd points to an alive process, we can simply use `pidfd_open()` to create the pidfd.

## Restoring `pidfd(s)` Pointing to a Dead Process

If the process (to which the pidfd was pointing) is dead, however, we lose access to its pid (`/proc/$pid/fdinfo/$pidfd/NSpid` becomes -1).
We also cannot open a pidfd pointing to a dead process.

To overcome these problems, we do the following things:

We create a `hashtable` with the `inode number` of the pidfd acting as the key that stores:
- List of all processes that had a pidfd open with this inode number.
- The highest id in the list of processes (`creator_id`)

For each unique `inode number`, the process with `creator_id` (let's call it `creator`) creates a temporary process (let's name this process `x`).

The creator process will then open `pidfd(s)` pointing to `x` and will send them to all other processes (that had a pidfd with this inode number) using CRIU's `send_desc_to_peer` and `recv_desc_from_peer` functions, which allow processes to send and receive file descriptors.

This way, all processes with pidfds that point to the same dead process remain in the correct state (i.e., have pidfds with the same inode number) even after restoration.

After the creator process finishes opening and sending all pidfds, it kills the temporary process `x`.

## Limitations

- We currently do not support pidfd's opened with the `PIDFD_THREAD` flag.
- We also cannot C/R pidfd's that point to process(es) outside the current process tree.

