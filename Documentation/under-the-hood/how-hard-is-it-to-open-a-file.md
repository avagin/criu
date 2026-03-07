# How hard is it to open a file

This article outlines what CRIU restore needs to take care of when re-creating an open file descriptor.

Let's imagine we have an information about a file we want to open.
What should it contain? Apparently, access mode and path:

```C

	struct file {
		char *path;
		unsigned mode;
	} *f;

```

and we'd like to have that path being opened by a process.  We would
do it like below:

```C

	int fd;

	fd = open(f->path, f->mode);

```

Right? Right, but it's not all of it. We all know, that not only regular
files might be opened via paths, but also such things as FIFOs. And
plain `open()` with the flags we want it to have may just hang. So we need
to change that code to look like this:

```C

	int fd, tfd = -1;

	if (S_ISFIFO(f->mode))
		tfd = open(f->path, O_RDWR);

	fd = open(f->path, f->mode);

	if (tfd >= 0)
		close(tfd);

```

The tfd keeps FIFO read-write opened while we open it with any flags
we want. Then we close it.

Now this seems to be OK, but it's actually not. In Linux, file can be
unlinked while being opened (these [invisible files](invisible-files.md) are treated carefully
on dump). In that case what was formerly pointed by
path may be kept in some temporary location. We have to create a
temporary name for it, and unlink it afterwards. So, we need to extend the
info about a file:

```C

	struct file {
		char *path;
		unsigned mode;
		char *temp_path;
	} *f;

```

and the opening code to take care of that temporary location

```C

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

And we haven't seen all the code we need to manage what is pointed by
the `temp_path`, but let's proceed.

We have forgotten, that opened and <s>unlinked</s> removed can also be a
directory. For directories, link and unlink do not work, and we have to
append the code to at least try to make things work OK:

```C

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
			rmdir(f->mode);
		else
			unlink(f->path);
	}

```

Done. Oh wait, we also should take care of hard links! If a file has any,
and both were opened and removed, we cannot just go
ahead and kill the `temp_path` after opening, as
it can be waiting for some other
`struct file` to open one. A little bit more information should be added
to the `struct file`.

```C

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

and to the code that opens one now looks like this:

```C

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
				rmdir(f->mode);
			else
				unlink(f->temp->path);
		}
	}

```

By the way, we've left behind the scenes all the code required to make
the `temp_file` data be shared between processes that need one and to
make the decrementing of `f->temp->users` be SMP-safe.

Also note, that we don't handle the case when the file/directory is
removed and some other file/directory is created under the same name.
It's a rare case.

Now, is that all? No, sorry. A couple of things left. First, Linux has
a thing called mount namespace. And two files with the same path may
have been opened in different mount namespaces. So we also need the
info about what mount point the file belongs to like this:

```C

	struct file {
		char *path;
		unsigned mode;
		struct temp_file *temp;
		unsigned mnt_id;
	} *f;

```

and the code to open file would now look like

```C

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

	if (f->temp_path) {
		if (--f->temp->users == 0) {
			if (S_ISDIR(f->mode))
				unlinkat(ns_fd, f->mode, AT_REMOVEDIR);
			else
				unlinkat(ns_fd, f->temp->path);
		}
	}

	close(ns_fd);

```

Let's not dive into the details of how the `open_ns_root` looks like.
Just know, that it opens a file descriptor, that refers to the root
of the mount namespace that contains a mount point with the id `mnt_id`
(they cannot be shared, and that's great).

Pretty complex already, isn't it? Just a couple of final touches left.
First, opened files typically have a position. Flags we get need to be
sanitated not to container those that only make sense during open,
like `O_TRUNC` or `O_CREAT`. And file may have a thing called `fown` managed
by the `F_SETSIG` and `F_SETOWN` fcntls. All this results in

```C

	struct file {
		char *path;
		unsigned mode;
		struct temp_file *temp;
		unsigned mnt_id;
		unsigned long pos;
		struct fown fown;
	} *f;

```

and

```C

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

	if (f->temp_path) {
		if (--f->temp->users == 0) {
			if (S_ISDIR(f->mode))
				unlinkat(ns_fd, f->mode, AT_REMOVEDIR);
			else
				unlinkat(ns_fd, f->temp->path);
		}
	}

	close(ns_fd);

	fcntl(fd, F_SETSIG, f->fown->sig);
	fcntl(fd, F_SETOWN, &f->fown->owner);
	lseek(fd, SEEK_SET, f->pos);

```

And don't ask for details of the `f->fown` thing. It's tricky, but just
follows the ABI and therefore boring.

OK, we've finished with the top of the iceberg — opening a file. Why
top? Becase when opened file should be planted into a process' file
descriptors table under desired number. You might think that it should be
as simple as:
```C

	dup2(fd, desired_fd);

```

but it's not. Here's [how to assign needed file descriptor to a file](how-to-assign-needed-file-descriptor-to-a-file.md).


