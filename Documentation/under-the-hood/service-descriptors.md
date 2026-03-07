# Service descriptors

These are the fds that help us to do checkpoint and restore. There must be only files, which are used by criu frequently, and it's difficult to obtain them by another way. The number of service fds on restore is significant, as they make file table grow, so service fds may increase memory usage after restore in comparison to dump. Other common used on restore files should be placed in fdstore instead. Note, that not all members of enum sfd_type from criu/include/servicefd.h have to be there. Some of them must go to fdstore sometimes.

## Restore
-We try to place service fds as low as possible to minimize memory overhead. Per-fdtable variable service_fd_base points to the biggest service fd number. It's being chosen in choose_service_fd_base() in dependence of max file descriptor used by task.

-Since ordinary files, which are open on restore, may be bigger than service_fd_base, there is code parts where service fds mustn't be modified at all. We mask such areas via sfds_protected variable, and they aborts restore if modifications of sfds are found in such code parts.


