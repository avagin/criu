# How to assign needed file descriptor to a file

Let's imagine we have [opened a file](how-hard-is-it-to-open-a-file.md) and want it to have some exact descriptor number, not the one kernel gave to us.

The information we have is
```

	struct fd {
		struct file *file;
		int tgt_fd;
	} *fd;

```

and we've just done the
```

	int fd;

	fd = open_a_file(fd->file);

```

what's next? In Linux there's a cool system call `dup2()` which assigns to a file, referenced by one file descriptor, some other one, given by the caller.
So the code would look like this:
```

	int fd;

	fd = open_a_file(fd->file);
	dup2(fd, fd->tgt_fd);
	close(fd);

```

Now let's remember, that a file can be opened multiple times in one task, this happens when you e.g. start a shell. One of the `/dev/tty` or alike files will sit under 0, 1 and 2 descriptors. Not a big deal, we just expand the `struct fd`

```

	struct fd {
		struct file *file;
		int n_fds;
		int *tgt_fds;
	} *fd;

```

and the code itself:

```

	int fd, i;

	fd = open_a_file(fd->file);
	for (i = 0; i < fd->n_fds; i++)
		dup2(fd, fd->tgt_fds[i]);
	close(fd);

```

Next thing to handle -- file shared between tasks. This is also very typical, once you called `open()` and then `fork()` the file becomes such. But what if a file is shared between two processes, none of which is the ancestor of another? There are two ways of doing this, CRIU uses the most straightforward one -- it [sends file descriptors](http://linux.die.net/man/3/cmsg) between processes.

This requires some complication in the structures we use

```

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

and in the code which now consists of two parts -- one that opens file and sends it to others, and the other one that just receives them. We will come back to this again below, let's enjoy the code we have at the moment:

```

	int fd, i, pid = getpid(), sk;

	sk = create_socket();

	if (pid == file_opener(fd)) {
		fd = open_a_file(fd->file);

		for (i = 0; i < fd->n_fds; i++) {
			if (fd->tgt_fds[i].pid == pid)
				dup2(fd, fd->tgt_fds[i].fd);
			else
				send_fd(fd, fd->tgt_fds[i], sk);
		}

		close(fd);
	} else {
		for (i = 0; i < fd->n_fds; i++) {
			if (fd->tgt_fds[i].pid != pid)
				continue;

			fd = recv_fd(sk);
			dup2(fd, fd->tgt_fds[i].fd);
			close(fd);			
		}
	}

	close(sk);

```

Please, note, that all `tgt_fds` belonging to some task are opened by different one and then are sent to the real owner in the order they are met in the array. So does the receiver -- it receives the fds in the same order, so this algorithm puts files into proper descriptors.

There are several bugs in the above code snippet however :(

First, the `send_fd` and `recv_fd` routines cannot works using one socket for all tasks -- a descriptor sent to task `pid` should reach *this* task, not some arbitrary one that kernel woke up earlier on data arrival. That said, we have to create one socket per at least task to receive descriptors. But files can be shared in a tricky manner, so that task A may have one file shared with task B and some other file shared with task C. If the "who opens a file" voting selects B and C for respective files, they will have to send descriptors to A with proper coordination with each other. This coordination can be simplified if we create sockets not just per-pid, but per-(pid, fd). And where to keep all this bunch of sockets? The easiest answer -- in the places where the files they will receive should sit :) And we then use the `sendto()` syscall to send the descriptor via unconnected socket by address. These transport sockets may have some unique name like `"criu-fd-transport-%pid-%fd"`.

Second, when the file opener calls `dup2()` it may overwrite the `sk` descriptor. This is sad, but OK, since we can move the sk into any free place using plain `dup()` system call.

```

	int fd, i, pid = getpid(), sk;

	if (pid == file_opener(fd)) {
		sk = create_socket();
		fd = open_a_file(fd->file);

		for (i = 0; i < fd->n_fds; i++) {
			if (fd->tgt_fds[i].pid == pid) {
				if (sk == fd->tgt_fds[i].fd)
					sk = dup(sk);
				dup2(fd, fd->tgt_fds[i].fd);
			} else
				send_fd(fd, fd->tgt_fds[i], sk);
		}

		close(fd);
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

			fd = recv_fd(fd->tgt_fds[i].fd);
			dup2(fd, fd->tgt_fds[i].fd);
			close(fd);
		}
	}

```

This is almost all. Only a few notes left.

First, the above code is still buggy -- the file opener should make sure that everybody has their transport sockets ready. This requires some synchronization around `create_socket()` and `send_fd`.

Second, the "who will open a file" voting. We should make sure, that the synchronization mentioned above doesn't AB-BA deadlock, so when deciding which task to open a file we always chose the one with the smallest pid. And the file sending wave goes upwards the process tree :)

Third, the `open_a_file()` is not just [this](how-hard-is-it-to-open-a-file.md). Opened can be pipe, socket, signalfd, inotify and many other fancy stuff none of which uses the open-by-path engine.

And the last, but not least, files can depend on each other. E.g. an eventpoll file may have some other file descriptor monitored, and if we call the `open_a_file()` on eventpoll fd before we open the fd being monitored, we fail. This also affects the code that forms an array of `struct fd`-s


