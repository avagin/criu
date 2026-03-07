# FDInfo Engine

## Masters and Slaves
1. A file may be referenced by multiple file descriptors, which may belong to a single process or several different processes.
1. A group of descriptors referring to the same file is considered "shared." One descriptor is designated the **master**, while the others are **slaves**.
1. Every descriptor is represented by a `struct fdinfo_list_entry` (fle).
1. One process opens the master `fle` of a file, while other processes sharing the file obtain it using `SCM_RIGHTS`. See `send_fds()` and `receive_fds()` for details.

## Per-Process File Restore
Every file type is described by a `struct file_desc`. We sequentially call the `file_desc::ops::open(struct file_desc *d, int *new_fd)` method for every master file of a process until all masters are restored. The `open` methods can return three values:
- ` 0`: Restoration of the master file has successfully completed.
- ` 1`: Restoration is in progress or cannot yet start because of dependencies on other files; the method will be called again.
- `-1`: Restoration failed.

When a file is first opened, the `open` method must return the file descriptor value in the `new_fd` argument. This allows the core code to send this master to other processes so they can reopen it as a slave as soon as possible. Note that returning a non-negative `new_fd` does not necessarily mean the master is fully restored. The `open()` callback may return a non-negative `new_fd` while still returning `1` to indicate that work remains.

**Example: Restoring a connected UNIX socket**
1. Open a socket, write its file descriptor to `new_fd`, and return `1`.
1. Check if the peer socket is open and bound. If not, return `1` and retry later.
1. Connect to the peer and return `0`.

The peer that performs the `bind()` must notify the waiting socket once it is ready:
1. `bind(<peer name>)`
1. `set_fds_event(<socket pid>)`

## Dependencies
1. A slave TTY can only be created after its respective master peer is restored. Currently, we wait until all masters are restored.
1. The controlling terminal (CTTY) must be created after all other TTYs are restored. See `tty_deps_restored()` for details on TTY dependencies.
1. Epoll instances can be created at any time, but an FD can only be added to its polling list after the corresponding `fle` is completely restored. The exception is an epoll instance listening to another epoll instance; in this case, we only wait until the listened `fle` is created. See `epoll_not_ready_tfd()`.
1. A UNIX socket must wait for its peer before connecting. See `peer_is_not_prepared()` for details.
1. TCP sockets use a counter for address usage.
1. When implementing new relationships between `fle` stages, ensure you do not introduce circular dependencies.

## Notes
1. Pipes (and FIFOs), UNIX sockets, and TTYs generate two FDs in their `->open` callbacks. The second FD may conflict with another FD the task is restoring, and this second FD may need to be sent to another task.
