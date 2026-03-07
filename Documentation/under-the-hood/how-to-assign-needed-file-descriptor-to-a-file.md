# How to Assign a Needed File Descriptor to a File

Suppose we have [opened a file](how-hard-is-it-to-open-a-file.md) and want it to have a specific descriptor number rather than the one assigned by the kernel.

Given:
```c
struct fd {
    struct file *file;
    int tgt_fd;
} *fd;
```

And after calling:
```c
int fd_opened;
fd_opened = open_a_file(fd->file);
```

We can use the `dup2()` system call to assign the opened file to the target descriptor number:

```c
int fd_opened;
fd_opened = open_a_file(fd->file);
dup2(fd_opened, fd->tgt_fd);
close(fd_opened);
```

In some cases, a single file might be opened multiple times within a task (e.g., a shell where `/dev/tty` might be mapped to descriptors 0, 1, and 2). We can handle this by extending the `struct fd`:

```c
struct fd {
    struct file *file;
    int n_fds;
    int *tgt_fds;
} *fd;
```

The updated restoration logic:

```c
int fd_opened, i;
fd_opened = open_a_file(fd->file);
for (i = 0; i < fd->n_fds; i++)
    dup2(fd_opened, fd->tgt_fds[i]);
close(fd_opened);
```

Files shared between different tasks are also common (e.g., after a `fork()`). If a file is shared between two processes where neither is an ancestor of the other, CRIU handles this by [sending file descriptors](http://linux.die.net/man/3/cmsg) between processes.

This adds complexity to our structures:

```c
struct pid_fd {
    int pid;
    int fd;
};

struct fd {
    struct file *file;
    int n_fds;
    struct pid_fd *tgt_fds;
} *fd;
```

The logic now involves two parts: one process that opens the file and sends it to others, and other processes that receive it.

```c
int fd_opened, i, pid = getpid(), sk;
sk = create_socket();

if (pid == file_opener(fd)) {
    fd_opened = open_a_file(fd->file);

    for (i = 0; i < fd->n_fds; i++) {
        if (fd->tgt_fds[i].pid == pid)
            dup2(fd_opened, fd->tgt_fds[i].fd);
        else
            send_fd(fd_opened, fd->tgt_fds[i], sk);
    }
    close(fd_opened);
} else {
    for (i = 0; i < fd->n_fds; i++) {
        if (fd->tgt_fds[i].pid != pid)
            continue;

        fd_opened = recv_fd(sk);
        dup2(fd_opened, fd->tgt_fds[i].fd);
        close(fd_opened);			
    }
}
close(sk);
```

All `tgt_fds` belonging to a task are opened by another task and sent to the owner in the order they appear in the array. The receiver processes them in the same order, ensuring files are placed into the correct descriptors.

However, the logic above has some flaws:

1.  **Transport Coordination**: `send_fd` and `recv_fd` cannot reliably use a single shared socket for all tasks. A descriptor intended for a specific PID must reach that task. CRIU creates dedicated sockets for receiving descriptors, often named uniquely like `criu-fd-transport-%pid-%fd`.
2.  **Descriptor Overwriting**: When the opener calls `dup2()`, it might overwrite the transport socket descriptor (`sk`). To avoid this, `sk` can be moved to a free descriptor using `dup()`.

Revised logic:

```c
int fd_opened, i, pid = getpid(), sk;

if (pid == file_opener(fd)) {
    sk = create_socket();
    fd_opened = open_a_file(fd->file);

    for (i = 0; i < fd->n_fds; i++) {
        if (fd->tgt_fds[i].pid == pid) {
            if (sk == fd->tgt_fds[i].fd)
                sk = dup(sk);
            dup2(fd_opened, fd->tgt_fds[i].fd);
        } else {
            send_fd(fd_opened, fd->tgt_fds[i], sk);
        }
    }
    close(fd_opened);
    close(sk);
} else {
    for (i = 0; i < fd->n_fds; i++) {
        if (fd->tgt_fds[i].pid != pid)
            continue;

        sk = create_socket();
        dup2(sk, fd->tgt_fds[i].fd);
    }

    for (i = 0; i < fd->n_fds; i++) {
        if (fd->tgt_fds[i].pid != pid)
            continue;

        fd_opened = recv_fd(fd->tgt_fds[i].fd);
        dup2(fd_opened, fd->tgt_fds[i].fd);
        close(fd_opened);
    }
}
```

Additional Considerations:

- **Synchronization**: The opener must ensure that all transport sockets are ready before sending descriptors.
- **Deadlock Prevention**: To avoid deadlocks during synchronization, the task with the smallest PID is chosen to open a shared file, and descriptors are sent "upwards" through the process tree.
- **Complexity of `open_a_file()`**: Opening a file is not always about a path; it could involve pipes, sockets, or other specialized objects.
- **Dependencies**: Files can depend on one another. For example, an `eventpoll` descriptor might monitor other descriptors. If the `eventpoll` is opened before its monitored descriptors, it will fail.
