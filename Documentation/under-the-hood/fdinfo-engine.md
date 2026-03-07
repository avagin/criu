# Fdinfo engine

= Masters and slaves =
1. A file may be referred to by several file descriptors. The descriptors may belong to a single process or to several processes.
1. A group of descriptors referring to the same file is called shared. One of the descriptors is named master, others are slaves.
1. Every descriptor is described via struct fdinfo_list_entry (fle).
1. One process opens a master fle of a file, while other processes, sharing the file, obtain it using scm_rights. See send_fds() and receive_fds() for details.

= Per-process file restore =
Every file type is described via structure file_desc. We sequentially call file_desc::ops::open(struct file_desc *d, int *new_fd) method for every master file of a process until all masters are restored. The open methods may return three values:
-  0  -- restore of the master file is successefuly finished;
-  1  -- restore is in progress or it can't be started yet, because of it depends on another files, so the method should be called once again;
- -1 -- restore failed.

Right after a file is opened for the first time, the open method must return the fd value in the new_fd argument. This allows the common code to send this master to other processes to reopen the master as a slave as soon as possible. At the same time, returning a non-negative new_fd does not mean that the master is restored. The open() callback may return a non-negative new_fd and "1" as return value at the same time.

Example. Restore of connected unix socket by open() method.
-1)Open a socket, write its file descriptor to new_fd and return 1.
-2)Check if peer socket is open and bound. If it's not so, then return 1 and repeat step "2" in next time.
-3)Connect to the peer and return 0.

Note: it's also possible to go to step "2" right after new_fd is written.

The peer, which bind() the socket waits in "2", must notify the socket, when it is bound:
-1)bind(<peer name>);
-2)set_fds_event(<socket pid>);

= Dependencies =
1. Slave TTY can only be created after respective master peer is restored. But now we wait even more -- till all masters are restored.
1. CTTY must be created after all other TTYs are restored. For all tty dependencies see tty_deps_restored() for the details.
1. Epoll can be created in any time, but it can add a fd in its polling list after the corresponding fle is completely restored. The only exception is a epoll listening other epoll. In this case we wait till listened fle is just created (not restored). See epoll_not_ready_tfd().
1. Unix socket must wait a peer before connect to it. See peer_is_not_prepared() for the details.
1. TCP sockets have a counter on address use.
1. Implementing a new relationships between fle stages, check, that you are not introducing a circular dependence (with existing).

= Notes =
1. Pipes (and fifos), unix sockets and TTYs generate two fds in their ->open callbacls, the 2nd one can conflict with some other fd the task restores and (!) this "2nd one" may require sending to some other task.


