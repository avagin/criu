# Service Descriptors

Service descriptors are file descriptors (FDs) used by CRIU to facilitate the checkpoint and restore processes.

Only files that are used frequently by CRIU and are difficult to obtain by other means should be maintained as service FDs. Because these FDs cause the file table to grow, they can result in higher memory usage after restoration compared to the initial dump. Other files used during restoration should generally be placed in the `fdstore` instead. Note that not all members of the `sfd_type` enum in `criu/include/servicefd.h` are required to be present at all times; some may be moved to the `fdstore` when appropriate.

## Restore Process

CRIU attempts to assign service FDs to the lowest possible numbers to minimize memory overhead. The `service_fd_base` variable tracks the highest service FD number and is determined by `choose_service_fd_base()` based on the maximum file descriptor used by the task.

Because standard files opened during restoration may have descriptors higher than `service_fd_base`, certain sections of code are marked where service FDs must not be modified. These areas are protected via the `sfds_protected` variable; if an unauthorized modification to a service FD is detected in these sections, the restoration process is aborted.
