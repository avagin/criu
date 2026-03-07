# Filesystem Peculiarities

"All filesystems are equal, but some filesystems are more equal than others."

This page describes how different filesystems affect the CRIU dump and restore processes.

## BTRFS

When calling `stat()` on a file, we can determine the device it resides on by checking the `st_dev` value. However, the kernel exposes device values in several other places, such as `/proc/$pid/mounts`, `/proc/$pid/mountinfo`, and `/proc/$pid/smaps`. Additionally, the `sock-diag` subsystem reveals the device and inode for bound UNIX sockets.

BTRFS is problematic because it replaces the real device number with a virtual one in the `stat()` system call. This virtual device number cannot be compared to device numbers obtained from other sources, as they will always differ.

To address this, CRIU performs path-to-device resolution in userspace by analyzing information from `/proc/$pid/mountinfo`. This logic is implemented in `mount.c:phys_stat_resolve_dev()`.

### BTRFS Workaround
One possible workaround for using BTRFS with CRIU is to disable Copy-on-Write (COW). For example, to use Podman's checkpoint/restore support on BTRFS, you can use: `chattr +C /var/lib/containers`.

## NFS

In Linux, files have an `st_nlink` attribute representing the number of names (hard links) pointing to the file. When a file is unlinked, this counter is decremented; if it reaches zero, the file can be physically removed from the disk. However, if a process still holds the file open, physical removal is delayed until the file is closed.

NFS handles this differently. If an NFS client sent the final `unlink` request, the server would immediately delete the file, unaware that the client still has it open. To prevent this, the client marks the file for deletion upon closing and renames it to a special name (e.g., `.nfsXXX`). This is known as "NFS silly rename."

How does this affect CRIU? As discussed in [How hard is it to open a file?](how-hard-is-it-to-open-a-file.md), CRIU must be able to dump and restore open but unlinked files. Normally, CRIU identifies these and stores their content in images. On NFS, however, unlinked files do not appear to have an `nlink` count of zero because of the silly rename.

To handle this, CRIU checks if a file resides on NFS (via `statfs`). If it does, CRIU checks if the filename follows the silly-rename pattern. If both are true, the file is treated as "open and unlinked." This logic is in `files-reg.c:nfs_silly_rename()`.

## AUFS

AUFS is not in the upstream kernel, but it is used by Docker and supported by CRIU.

This filesystem has a known issue: when a file is executed (`execv`), the mappings in `/proc/$pid/maps` or `/proc/$pid/smaps` may show "wrong" paths. AUFS combines multiple subdirectories (branches) into one. A file accessed via an AUFS path actually resides in one of these branches. In certain cases, `/proc` shows the path within the branch rather than the AUFS path.

This is problematic because CRIU needs the AUFS path to properly restore the file. To fix this, CRIU identifies AUFS mounts, reads branch information from `sysfs`, and "fixes" the paths by mapping branch-specific paths back to their AUFS equivalents. This logic is in `sysfs_parse.c:fixup_aufs_vma_fd`.
