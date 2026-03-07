# Invisible Files

In Linux, files may be inaccessible via `open()` yet still exist within the system. This page explains how this occurs and how CRIU handles these situations.

## How a File Can Lose Its Path

A common scenario involves a process performing the following:

```c
int fd = open("/foo/bar", O_RDONLY);
unlink("/foo/bar");
```

After the `unlink()`, the name `/foo/bar` is removed from the filesystem, but because the process still holds an open file descriptor, the file's data remains on disk.

There are two primary sub-cases. First, if the file has no other hard links, the data is truly "orphaned." Second, if other hard links exist, the file still has a name elsewhere. However, the Linux VFS layer generally does not provide a direct way to find those other names.

### Virtual Filesystems

On virtual filesystems like `proc` or `sysfs`, invisible files can appear if the underlying object is removed. For example, if a process opens a file in `/proc/$pid` and that task subsequently dies, the path is removed, but the file descriptor remains open (though subsequent reads will return `ENOENT`).

### Name-less Files

Some files are created without ever having a name. This is discussed in [another article](how-to-open-a-file-without-open-system-call.md).

### Overmounted Files

If a task opens a file and a new mount point is later mounted over any part of its path, the original path may become inaccessible or point to a different file. CRIU currently does not support checkpointing such files and will abort the dump.

## How CRIU Handles Invisible Files

### Detection and Dumping

Subject to certain [filesystem peculiarities](filesystems-pecularities.md), CRIU detects invisible files as follows:

1.  CRIU [retrieves the file descriptors](dumping-files.md) from the target process.
2.  For each FD, CRIU identifies the file's name by calling `readlink` on `/proc/self/fd/$fd`.
3.  CRIU then calls `fstat()` on the file descriptor.

If `st_nlink` is zero, the file has been fully deleted. Since it cannot be reopened by path, CRIU must save the file's contents directly into the image as a **ghost file**. By default, CRIU limits the size of ghost files it can checkpoint to 1 MB, though this can be adjusted with the `--ghost-limit` option.

If `st_nlink` is non-zero, CRIU verifies if the path retrieved from `proc` still refers to the same file. It calls `stat()` on that path and compares the `st_dev` and `st_inode` fields with those from the `fstat()` call. If they match, the file is still accessible by that name. If they do not match, the name now refers to a different file, and the dump fails (a rare situation that CRIU may support in the future).

A third possibility is that `stat()` fails with `ENOENT`, meaning the file has other names, but the one used to open it has been removed. In this case, CRIU uses the `linkat` system call to create a temporary name for the file on disk and records this name in the image. This is known as **link-remap**. Because this modifies the filesystem, the `--link-remap` option must be provided. These temporary names are removed after the restoration is complete.

### Chunked Ghost Files

When CRIU checkpoints a ghost file larger than 12 MB, it attempts to minimize the image size by identifying holes (sparse areas). This allows CRIU to save only the actual data chunks and record the offsets, skipping empty space. To optimize this process on highly sparse files, CRIU supports the `--ghost-fiemap` option, which uses the `fiemap` ioctl for better performance.

### Virtual Filesystems

For the `proc` filesystem, CRIU uses a different technique. If a name in `/proc` is dead, it cannot be linked or saved as a ghost file. Instead, CRIU records the PID of the deceased process. During restoration, CRIU creates a temporary task with that PID, which remains alive until all its openers have been restored.
