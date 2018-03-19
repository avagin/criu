#include "zdtmtst.h"

const char *test_doc	= "Run busy loop while migrating";
const char *test_author	= "Roman Kagan <rkagan@parallels.com>";

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

int error(int code, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	return code;
}

int pipe_read(int fdp)
{
	char buffer[1];
	return read(fdp, buffer, sizeof(buffer));
}

int pipe_write(int fdp)
{
	char buffer[1];
	return write(fdp, buffer, sizeof(buffer));
}

int set_next_ns_pid(pid_t pid)
{
	int fd, rc, len;
	char val[16];

	len = snprintf(val, sizeof(val), "%d", pid - 1);

	fd = open("/proc/sys/kernel/ns_last_pid", O_WRONLY);
	if (fd > -1) {
		rc = write(fd, val, len);
		close(fd);
	} else {
		rc = -1;
	}

	return rc;
}

int proc_open_cmdline(pid_t pid)
{
	char fname[256];
	snprintf(fname, sizeof(fname) - 1, "/proc/%d/cmdline", pid);
	return open(fname, O_RDONLY);
}

int main(int argc, char **argv)
{
	int fd, fdp[2], rc, status;
	pid_t pid;

	test_init(argc, argv);

	rc = set_next_ns_pid(32000);
	if (rc < 0)
		return error(-1, "PARENT: Failed to set next NS PID\n");

	rc = pipe(fdp);
	if (rc < 0)
		return error(-1, "PARENT: Failed to create pipe\n");

	pid = fork();

	if (pid < 0)
		return error(-1, "PARENT: Failed to fork child\n");

	if (pid == 0) {
		printf("CHILD: Enter, PID %d, PPID %d\n", getpid(), getppid());
		pipe_read(fdp[0]);
		/* CONTAINER */
		printf("CHILD: Exit\n");
		return EXIT_SUCCESS;
	}

	fd = proc_open_cmdline(pid);
	if (fd < 0)
		return error(-1, "PARENT: Failed to open child %d cmdline\n", pid);

	printf("PARENT: Resume child %d from parent %d\n", pid, getpid());

	pipe_write(fdp[1]);

	rc = waitpid(pid, &status, 0);
	if (rc != pid)
		return error(-1, "PARENT: Failed to wait for child\n");

	close(fdp[0]);
	close(fdp[1]);

	test_daemon();
	test_waitsig();

	pass();
	close(fd);

	return EXIT_SUCCESS;
}
