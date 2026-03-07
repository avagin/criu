# Mount points/2.0

= Problem =
A mount namespace is a tree of mount points. In addition, mounts have another type of dependencies which is called groups. Each mount can be a member of two groups, it can be a slave in one group and a member of another group. Currently groups can’t be set, it can be only inherited from a source mount. It is always a problem when more than one type of properties have to restored for one call. This means that we have to find a sequence of steps to get a required state.
In case of mount namespaces, one more problem is over-mounts. A few mounts may be over-mounted or processes can have file descriptors which are linked with over-mounted files.
Another difficulty is that we are not able to create bind-mounts between namespaces, but each file system have to be mounted from a specified user namespace.

= Solution =
When we see all variation of commands to build a mount tree, we can understand that the final picture may be very complicated to be repeated, so we suggest to add [a new flag](https://patchwork.kernel.org/patch/9703885/) to the mount() syscall, which allows us to add a mount into an existing group.

In this case the restore algorithm will be very simple.
- Create a temporary mount which is called “root yard”
- Create all namespaces (in specified user namespaces)
- Add root yards from all namespaces into one shared group, so a mount is created in one mntns, will be propagated into others.
- Create all mounts in separate directories in the root yards.
- Restore opened files (nothing is over-mounted at this point)
- Build mount trees in namespaces by moving mounts to right places
- Do pivot_root() in all namespaces

Let’s look at the next example:

{| class="wikitable"
!mnt_id
!parent
!shared
!master
|-
|1
|0
|
|
|-
|2
|1
|1
|
|-
|3
|2
|2
|
|-
|4
|2
|3
|
|-
| 5
| 1
|
|
|-
| 6
| 0
|
|
|-
|7
| 6
| 1
|
|-
| 8
| 7
| 2
| 
|-
| 9
| 7
| 4
| 3
|-
| 10
| 6
|
|
|}

The origin tree looks like this:

￼![File:mntns-2.0-tree.svg](File:mntns-2.0-tree.svg)

The first stage is to restore all mounts in all namespace separately. In addition, we need to create all shared groups.

￼![File:mntns-2.0-tree-2.svg](File:mntns-2.0-tree-2.svg)
￼

The next step is to move mounts to proper places to restore a tree and then we restore groups for each mount.

￼![File:mntns-2.0-tree-3.svg](File:mntns-2.0-tree-3.svg)

= Restore of unix sockets =

Unix sockets can be bound to a file. The problem is that an address and a file are not connected between each other in term of unix sockets. For example, if you move a socket file, ss shows the origin address and you can’t find a file where the socket is bound. Another example is that an address may contain a relative path (../socket_name).

Currently socket_diag shows a device and an inode number for a socket file, but it says nothing about a path to this file and about its mount point. We introduced the SIOCUNIXFILE ioctl, which returns a file descriptor to a socket file.
In this case to restore a unix socket we have to:
- create a temporary directory and mount tmpfs into it before restoring sockets
- Restore sockets
- create a socket address directory where is the last part is a symlink to a proper directory on a required mount point
- call chroot() to the temporary directory
- bind the socket to a specified address
if we restored a server socket, we can get a file descriptor for its file and use it to restore client sockets by calling connect() for /proc/self/fd/[SK_FILE_FD]
umount tmpfs from the temporary directory and remove the directory after restoring all sockets

= Source code =
- [github.com/avagin/criu/tree/mntns-2.0](https://github.com/avagin/criu/tree/mntns-2.0)
- [[PATCH](https://lkml.org/lkml/2017/5/9/634) fs: add an ioctl to get an owning userns for a superblock]

