# How Hard is it to Open a File?

This article outlines what CRIU must handle when recreating an open file descriptor during restoration.

Suppose we have information about a file we want to open, such as its access mode and path:

```c
struct file {
    char *path;
    unsigned mode;
} *f;
```

To have this path opened by a process, we might try:

```c
int fd;
fd = open(f->path, f->mode);
```

However, this is insufficient. Regular files are not the only things opened via paths; FIFOs are another example. A standard `open()` on a FIFO with certain flags might hang indefinitely. To prevent this, the code must be modified:

```c
int fd, tfd = -1;

if (S_ISFIFO(f->mode))
    tfd = open(f->path, O_RDWR);

fd = open(f->path, f->mode);

if (tfd >= 0)
    close(tfd);
```

Using `tfd` to keep the FIFO open for reading and writing ensures the subsequent `open()` succeeds regardless of the flags.

Even this revised approach has issues. In Linux, a file can be unlinked while it is still open (these [invisible files](invisible-files.md) require careful handling during a dump). If a file is unlinked, the path it once occupied may no longer exist or may point to something else. We must create a temporary name for it and unlink it after opening. Thus, we extend our file information and the opening logic:

```c
struct file {
    char *path;
    unsigned mode;
    char *temp_path;
} *f;
```

```c
int fd, tfd = -1;

if (f->temp_path)
    link(f->temp_path, f->path);

if (S_ISFIFO(f->mode))
    tfd = open(f->path, O_RDWR);

fd = open(f->path, f->mode);

if (tfd >= 0)
    close(tfd);

if (f->temp_path)
    unlink(f->path);
```

Directories can also be open while removed. Since `link()` and `unlink()` do not work for directories, we must adjust the logic:

```c
int fd, tfd = -1;

if (f->temp_path) {
    if (S_ISDIR(f->mode))
        mkdir(f->path);
    else
        link(f->temp_path, f->path);
}

if (S_ISFIFO(f->mode))
    tfd = open(f->path, O_RDWR);

fd = open(f->path, f->mode);

if (tfd >= 0)
    close(tfd);

if (f->temp_path) {
    if (S_ISDIR(f->mode))
        rmdir(f->path);
    else
        unlink(f->path);
}
```

Hard links introduce further complexity. If a file has multiple hard links that were all opened and then removed, we cannot simply delete the `temp_path` after opening it, as other `struct file` instances might still need it.

```c
struct temp_file {
    char *path;
    unsigned users;
};

struct file {
    char *path;
    unsigned mode;
    struct temp_file *temp;
} *f;
```

The opening logic then becomes:

```c
int fd, tfd = -1;

if (f->temp) {
    if (S_ISDIR(f->mode))
        mkdir(f->path);
    else
        link(f->temp->path, f->path);
}

if (S_ISFIFO(f->mode))
    tfd = open(f->path, O_RDWR);

fd = open(f->path, f->mode);

if (tfd >= 0)
    close(tfd);

if (f->temp) {
    if (--f->temp->users == 0) {
        if (S_ISDIR(f->mode))
            rmdir(f->path);
        else
            unlink(f->temp->path);
    }
}
```

Note that making `temp_file` data sharable across processes and ensuring `f->temp->users` is updated safely in an SMP environment requires additional implementation details. We also do not currently handle the rare case where a file/directory is removed and replaced by another object of the same name.

Mount namespaces add another layer of complexity. Two files with the same path may reside in different mount namespaces. We must track the mount point ID:

```c
struct file {
    char *path;
    unsigned mode;
    struct temp_file *temp;
    unsigned mnt_id;
} *f;
```

The opening logic then uses `open_ns_root()` to access the correct mount namespace:

```c
int fd, tfd = -1, ns_fd;
char *rel_path = f->path + 1;

ns_fd = open_ns_root(f->mnt_id);

if (f->temp) {
    if (S_ISDIR(f->mode))
        mkdirat(ns_fd, rel_path);
    else
        linkat(ns_fd, f->temp->path, ns_fd, rel_path);
}

if (S_ISFIFO(f->mode))
    tfd = openat(ns_fd, rel_path, O_RDWR);

fd = openat(ns_fd, rel_path, f->mode);

if (tfd >= 0)
    close(tfd);

if (f->temp) {
    if (--f->temp->users == 0) {
        if (S_ISDIR(f->mode))
            unlinkat(ns_fd, rel_path, AT_REMOVEDIR);
        else
            unlinkat(ns_fd, f->temp->path, 0);
    }
}

close(ns_fd);
```

Finally, open files have attributes like current position and ownership (`fown`) that must be restored. Flags like `O_TRUNC` and `O_CREAT` must also be sanitized.

```c
struct file {
    char *path;
    unsigned mode;
    struct temp_file *temp;
    unsigned mnt_id;
    unsigned long pos;
    struct fown fown;
} *f;
```

```c
int fd, tfd = -1, ns_fd, open_flags;
char *rel_path = f->path + 1;

ns_fd = open_ns_root(f->mnt_id);

if (f->temp) {
    if (S_ISDIR(f->mode))
        mkdirat(ns_fd, rel_path);
    else
        linkat(ns_fd, f->temp->path, ns_fd, rel_path);
}

if (S_ISFIFO(f->mode))
    tfd = openat(ns_fd, rel_path, O_RDWR);

open_flags = sanitize_open_mode(f->mode);
fd = openat(ns_fd, rel_path, open_flags);

if (tfd >= 0)
    close(tfd);

if (f->temp) {
    if (--f->temp->users == 0) {
        if (S_ISDIR(f->mode))
            unlinkat(ns_fd, rel_path, AT_REMOVEDIR);
        else
            unlinkat(ns_fd, f->temp->path, 0);
    }
}

close(ns_fd);

fcntl(fd, F_SETSIG, f->fown.sig);
fcntl(fd, F_SETOWN, &f->fown.owner);
lseek(fd, f->pos, SEEK_SET);
```

This covers the basics of opening the file. Once opened, it must be assigned to the correct file descriptor number in the process's FDT. While `dup2(fd, desired_fd)` seems simple, the reality is more involved, as explained in [How to assign a needed file descriptor to a file](how-to-assign-needed-file-descriptor-to-a-file.md).
