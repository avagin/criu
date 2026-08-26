#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "zdtmtst.h"

const char *test_doc = "Check that O_RDONLY|O_NONBLOCK fifos keep their POLLHUP state after restore";
const char *test_author = "Emir Buljubasic <emir.buljubasic@bicomsystems.com>";

char *filename;
TEST_OPTION(filename, string, "file name", 1);

#define FIFO_SIZE (1 << 20)

static int hup(int fd, int timeout)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	int ret;

	ret = poll(&pfd, 1, timeout);
	if (ret < 0) {
		pr_perror("poll() failed");
		return -1;
	}

	return (ret > 0 && (pfd.revents & (POLLHUP | POLLERR)));
}

static int open_fifo_ro(const char *path)
{
	int fd;

	fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		pr_perror("can't open %s", path);
		return -1;
	}

	return fd;
}

int main(int argc, char **argv)
{
	char hup_name[PATH_MAX];
	int fd[2], fd_hup[2], fdw;
	char buf[1];
	int  i;

	test_init(argc, argv);

	snprintf(hup_name, sizeof(hup_name), "%s.hup", filename);

	if (mknod(filename, S_IFIFO | 0600, 0)) {
		pr_perror("can't make fifo \"%s\"", filename);
		return -1;
	}
	if (mknod(hup_name, S_IFIFO | 0600, 0)) {
		pr_perror("can't make fifo \"%s\"", hup_name);
		return -1;
	}

	for (i = 0; i < 2; i++) {
	/*
	 * The read end is opened before any writer shows up -- this mirrors
	 * how openrc-init holds its control fifo. The kernel suppresses
	 * POLLHUP for such a reader, so poll() blocks instead of spinning.
	 */
	fd[i] = open_fifo_ro(filename);
	if (fd[i] < 0)
		return 1;

	if (fcntl(fd[i], F_SETPIPE_SZ, FIFO_SIZE) < 0) {
		pr_perror("can't set pipe size on %s", filename);
		return 1;
	}

	/*
	 * The second fifo has seen a writer come and go, so its read end is
	 * a closed pipe and has to keep reporting POLLHUP.
	 */
	fd_hup[i] = open_fifo_ro(hup_name);
	if (fd_hup[i] < 0)
		return 1;

	fdw = open(hup_name, O_WRONLY);
	if (fdw < 0) {
		pr_perror("can't open %s for writing", hup_name);
		return 1;
	}
	close(fdw);

	/* Sanity checks before C/R. */
	switch (hup(fd[i], 0)) {
	case -1:
		pr_err("poll() failed before C/R\n");
		return 1;
	case 1:
		pr_err("fifo reports POLLHUP before C/R\n");
		return 1;
	}

	switch (hup(fd_hup[i], 0)) {
	case -1:
		pr_err("poll() failed before C/R\n");
		return 1;
	case 0:
		pr_err("closed fifo doesn't report POLLHUP before C/R\n");
		return 1;
	}
	}

	test_daemon();
	test_waitsig();

	for (i = 0; i < 2; i++) {
	/*
	 * After restore poll() must still not report POLLHUP. If it does,
	 * CRIU reopened the reader while a (fake) writer was present, and the
	 * application would busy-loop at 100% CPU.
	 */
	switch (hup(fd[i], 100)) {
	case -1:
		fail("poll() failed after restore");
		return 1;
	case 1:
		fail("fifo reports POLLHUP after restore -- would busy-loop");
		return 1;
	}

	/*
	 * The pipe size must survive the restore even though CRIU reopens
	 * the reader against a writer-less (freshly created) pipe object.
	 */
	if (fcntl(fd[i], F_GETPIPE_SZ) != FIFO_SIZE) {
		fail("fifo lost its pipe size after restore");
		return 1;
	}

	/* A fifo whose writer is gone has to stay hung up. */
	switch (hup(fd_hup[i], 100)) {
	case -1:
		fail("poll() failed after restore");
		return 1;
	case 0:
		fail("closed fifo doesn't report POLLHUP after restore");
		return 1;
	}

	if (read(fd_hup[i], buf, sizeof(buf)) != 0) {
		fail("closed fifo hasn't been restored as closed");
		return 1;
	}

	if (close(fd[i]) < 0 || close(fd_hup[i]) < 0) {
		fail("can't close fifos");
		return 1;
	}
	}

	if (unlink(filename) < 0 || unlink(hup_name) < 0) {
		fail("can't unlink fifos");
		return 1;
	}

	pass();
	return 0;
}
