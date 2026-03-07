# Pidfd

This article describes how CRIU checkpoints and restores `pidfds` (process file descriptors).

## Checkpointing
All information required to restore a `pidfd` is available in the `/proc/$pid/fdinfo/$pidfd` entry.

Since CRIU does not currently support nested PID namespaces, the correct PID to use during restoration is the final entry in the `NSpid` field (representing the PID in the most deeply nested namespace). CRIU captures only this PID.

## Restoring Pidfds for Active Processes
During restoration, CRIU first recreates the entire process tree and then begins opening file descriptors for each process. If a `pidfd` points to a process that is already alive, CRIU simply uses the `pidfd_open()` system call to recreate it.

## Restoring Pidfds for Terminated Processes
If the process originally referenced by the `pidfd` has terminated, the PID information in `proc` is lost (the `NSpid` field becomes -1), and `pidfd_open()` cannot be used.

To handle this, CRIU uses the following mechanism:

1. A hash table is created using the `pidfd`'s inode number as the key. It stores:
    - A list of all processes that held a `pidfd` with that inode number.
    - A designated `creator_id` (the highest ID in the list).

1. For each unique inode number, the process identified as the `creator` creates a temporary process (let's call it `x`).

1. The `creator` process then opens a `pidfd` pointing to `x` and transmits it to all other processes that originally held a `pidfd` with the same inode number. This is done using CRIU's `send_desc_to_peer` and `recv_desc_from_peer` functions.

This ensures that all processes with `pidfds` pointing to the same terminated process are restored to a consistent state, sharing the same inode number. Once the `pidfds` have been distributed, the `creator` terminates the temporary process `x`.

## Limitations

- CRIU does not currently support `pidfds` opened with the `PIDFD_THREAD` flag.
- CRIU cannot checkpoint or restore `pidfds` that point to processes outside the captured process tree.
