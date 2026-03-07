# Mount Points 2.0

## The Problem
A mount namespace is a tree of mount points. In addition, mounts have group-based dependencies: a mount can be a slave in one group and a member of another. Currently, these groups cannot be set directly; they must be inherited from a source mount. Recreating these properties alongside other mount attributes requires a specific sequence of operations.

Additional challenges include:
- **Over-mounts**: Multiple mounts may occupy the same location, or processes may hold file descriptors to over-mounted files.
- **User Namespaces**: Bind mounts cannot be created between different namespaces directly; each filesystem must be mounted from its respective user namespace.

## The Solution
Given the complexity of recreating a mount tree using standard commands, we proposed [a new flag](https://patchwork.kernel.org/patch/9703885/) for the `mount()` system call that allows adding a mount to an existing group.

With this capability, the restoration algorithm is simplified:
1. Create a temporary "root yard" mount.
1. Create all necessary namespaces (within their respective user namespaces).
1. Add the root yards from all namespaces into a single shared group so that a mount created in one namespace propagates to others.
1. Create all mounts in separate directories within the root yards.
1. Restore open files (at this stage, no mounts are over-mounted).
1. Assemble the final mount trees by moving mounts to their correct locations.
1. Perform `pivot_root()` in all namespaces.

### Example Configuration

| mnt_id | parent | shared | master |
| :--- | :--- | :--- | :--- |
| 1 | 0 | | |
| 2 | 1 | 1 | |
| 3 | 2 | 2 | |
| 4 | 2 | 3 | |
| 5 | 1 | | |
| 6 | 0 | | |
| 7 | 6 | 1 | |
| 8 | 7 | 2 | |
| 9 | 7 | 4 | 3 |
| 10 | 6 | | |

The original tree:

![File:mntns-2.0-tree.svg](File:mntns-2.0-tree.svg)

First, each mount and shared group is restored separately within its respective namespace:

![File:mntns-2.0-tree-2.svg](File:mntns-2.0-tree-2.svg)

Finally, the mounts are moved into their proper positions and their group relationships are established:

![File:mntns-2.0-tree-3.svg](File:mntns-2.0-tree-3.svg)

## Restoring UNIX Sockets

UNIX sockets can be bound to files. However, a socket's address and its underlying file are not intrinsically linked within the kernel. For example, if a socket file is moved, tools like `ss` still show the original address. Addresses may also use relative paths (e.g., `../socket_name`).

While `socket_diag` provides the device and inode of a socket file, it does not provide the path or mount point. To address this, we introduced the `SIOCUNIXFILE` ioctl, which returns a file descriptor for the socket file.

To restore a UNIX socket:
1. Create a temporary directory and mount `tmpfs` onto it.
1. Restore the sockets.
1. Create a socket address directory where the final component is a symlink to the correct directory on the required mount point.
1. `chroot()` into the temporary directory.
1. Bind the socket to the specified address.
1. If restoring a server socket, retrieve the file descriptor for its file and use it to restore client sockets by calling `connect()` on `/proc/self/fd/[SK_FILE_FD]`.
1. Unmount the temporary `tmpfs` and remove the directory.

## Source Code
- [github.com/avagin/criu/tree/mntns-2.0](https://github.com/avagin/criu/tree/mntns-2.0)
- [[PATCH] fs: add an ioctl to get an owning userns for a superblock](https://lkml.org/lkml/2017/5/9/634)
