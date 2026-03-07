# Invisible files

In Linux files may be inaccessible for open() but still be present in the system. This can happen in several ways and this page is about how this can happen and what CRIU does about it.

## How a file can lose its path

This is pretty simple. A process may do this series of operations
```

int fd = open("/foo/bar");
unlink("/foo/bar");

```
After it the /foo/bar file will have its name removed from the filesystem tree (and from the on-disk data too), but since the file is still held by the process (this structure is explained in the article about [dumping files](dumping-files.md)), the blob with data itself is still there.

There are two different sub-cases in this scenario. First, when the number of hard links associated with the file is zero, i.e. the /foo/bar name was the last one removed or the only it ever had. Another situation is when the link count is not zero. This means that some other name (hard link) for this file exists. In the latter case it is important to note that the Linux VFS layer typically does *not* allow to directly find out what the other name is.

### Virtual filesystems

For virtual filesystems like proc or sysfs there is another possibility when such invisible files may appear. It's the removal of the object represented of a file on this FS. In particular, if we open some file in /proc/$pid and the respective task dies the path of the opened file would get removed, while the file itself would be still alive (though reporting ENOENT error on any attempts to read from one).

### Name-less files

There is a third possibility for a file not to have a visible name but it has nothing to do with dumping opened files. This is described in [another article](how-to-open-a-file-without-open-system-call.md).

### Overmounted files

If a task opens a file and then a new mountpoint appears on any part of its path, the original file's path may become non existing or point to different file. 

CRIU doesn't work with such files yet, aborting the dump. However, there's a way to fix this.

Although in Linux there's no way to open a file by a name looking *under* certain mount points, there's the openat() call which may look up a file starting from arbitrary point which, in turn, can be over-mounted too.

## What CRIU does about it

### Detection and dumping

First of all, CRIU should detect this situation to take place. Modulo some [filesystems pecularities](filesystems-pecularities.md), this is done like this.

First, we [get the files](dumping-files.md) from the target process via unix socket. Then for each of them get the file's name via /proc by calling `readlink` on the /proc/self/fd/$fd path. It's important to note, that we readlink *self* FD to get the file's name we can work with. Next we `fstat()` the respective self file descriptor.

If the `st_nlink` field is zero, then the file is fully deleted from the system. Since no filesystems allow to recreate the name of such files, we have no other choice than to store the file itself into images. So we generate a so called *ghost file* in the image directory and copy the file contents into it. Since the content of the file is saved into images, CRIU has a limit for a maximum file size it can checkpoint. By default, this limit is set to 1Mb but it can be changed with the `--ghost-limit` option.

But what happens if the link count is not zero. Then we should check than the name we got from proc is the one with which we can see this file. So we call `stat()` on this name and compare `st_dev` and `st_inode` fields of it with those obtained from the fstat() call earlier. If they match the file is alive and we can just dump its name. If they don't the name we got references some other file and we fail the dump. This can be handled, but this situation is quite rare so we decided to implement support for it later. 

But there's also a 3rd possibility -- the `stat()` could fail with ENOENT error, which means that the file has names, but the one we have it opened by is removed. In *this* situation we cannot just save the file name in the images, since this name is not longer alive. Neither we can dump the file as ghost, as the same file can be accessed by some other name. And, as was said, there's no way to find this other name. Fortunately, in this case filesystems allow to create a new name for a file, so CRIU calls `linkat` system call creating a temporary name for this file on the disk and saves this name in the image. This is called *link-remap*. Since this manoeuvre modifies the filesystem, CRIU requires the special option *--link-remap* to be passed to it allowing this behaviour. On restore the link-remap names are removed after files restore.

Please note that a file may have been opened by many removed names, and for each a link-remap name should point to the same file, so while dumping and restoring CRIU keeps track of those names to inode mappings.

### Chunked ghost files

When CRIU checkpoints an invisible (ghost) file with size larger than 12MB, it would try to reduce the size of the corresponding image by seeking for holes (e.g., in sparse files). This approach allows CRIU to save the content of the file into a set of chunks and skip over the "empty" space in a file by keeping track of the offsets.

However, determining the offsets in highly sparse file might encounter a significant amount of expensive system calls. In order to reduce the overhead of dumping such ghost files, CRIU supports the `--ghost-fiemap` option that uses an optimized algorithm based on the fiemap ioctl.

### Virtual filesystems

For proc CRIU uses a slightly different trick. When we see dead name in /proc we cannot link() a new name or create a ghost file. Instead, we remember the PID of the process that died, and on restore create a temporary task with the desired pid, which gets killed right after all its open()-ers are restored.



