# Filesystems pecularities

"All filesystems are equal, but some filesystems are more equal than others."

This page describes how different filesystems affect the CRIU dump (and restore) process.

## BTRFS

When we `stat()` a file we can get on which device it resides by checking the `st_dev` value. However, kernel exposes the device value in some more places. In particular, the device is shown in the `/proc/$pid/[mounts|mountinfo]` files and in the `/proc/$pid/s?maps` ones. Moreover, the sock-diag subsystem recently added into the kernel, reveals the device (and inode) on which a unix socket is bound.

The problem with btrfs is that it substitutes the real device number with a virtual one in the `stat()` system call. And once we get this value we cannot compare it to any other device number obtained from other sources, they will always differ (these virtual device numbers are unique).

In order to address this issue, CRIU performs path-to-device resolution in user-space by analysing the information obtained from the `/proc/$pid/mountinfo` files. The routine in question is `mount.c:phys_stat_resolve_dev()`.

### BTRFS Workaround

One possible workaround to use BTRFS in combination with CRIU is to disable copy on write (COW). During the discussion in https://github.com/containers/podman/issues/9318 one workaround to use Podman's checkpoint/restore support was: `chattr +C /var/lib/containers`

## NFS

In Linux files have an attribute called `st_nlink` -- the amount of names the file has. When a file is removed (which is done with the `unlink` system call) this counter is decremented and, if it hits zero, the file itself can be removed from disk. Not "is removed", but "can be removed", since the file can be held opened while someone unlinks one. In the latter case the physical removal of the file is delayed till the file is closed.

NFS does special handing in case the nlink value is about to hit zero. The thing is -- if NFS client would send the last unlink request to the server, the latter would just go and kill the file physically, since it doesn't "know" that someone holds this file opened (this information is owned by the client). Instead, client marks the file as "to be deleted on close" and doesn't perform the last nlink decrement immediately. And only when the file is closed and the mentioned flag is seen, the last unlink is sent to the server. And one more thing -- to prevent naming collisions (in simple words -- `open()` by the old name shouldn't file this old file) NFS also renames the file, giving it a special name ".nfsXXX" where XXX is some unique identifier. This trick is called "NFS silly rename".

How does this affect CRIU? In the article "[How hard is it to open a file](how-hard-is-it-to-open-a-file.md)?" it's said, that CRIU should be able to dump and restore files, that are unlinked, but opened. Briefly: if a file is such, CRIU cannot just save the file's path, as once dumped tasks are killed, the fill would stop existing. So CRIU takes these files into the images. But on NFS there's no such thing as "unlinked" file -- it prevents the nlink count from dropping to zero. For CRIU all NFS files look as alive ones.

To handle this, CRIU checks that a file it dumps resides on NFS (this is simply by checking the `statfs`'s `fs_type` field). If the file is such CRIU then checks its name to be the silly-renamed one. If both checks succeed the file is treated as "opened and unlinked" one. The code in question is the `files-reg.c:nfs_silly_rename()`.

## AUFS

[AUFS](http://aufs.sourceforge.net) is not (yet) upstream, but Docker guys use one, so does CRIU.

This FS has a strange BUG -- when we `execv` some file on it, and then check for `/proc/$pid/maps` or `.../smaps` files or links in the `.../map_files/` directory, we would note, that those mappings, that correspond to the executed file are seen under "wrong" paths.

How wrong are these paths? AUFS joins several subdirectories into one, all these subdirs are called *branches*. And the file seen inside AUFS by one path "really" has some other one -- the path by the file is seen in the respective branch. So in proc in these strange cases we would see the path from branch, instead of the path, by which the file is seen in AUFS.

This is problematic, since CRIU needs to know the path by which the file was accessed inside AUFS in order to properly restore one. To fix this, CRIU checks that there's an AUFS mount in the game, reads the branches info from sysfs, and then, when meets an AUFS file in mappings, check for the path to belong to one of the branches and "fixes" one. This has sits in the `sysfs_parse.c:fixup_aufs_vma_fd`.


