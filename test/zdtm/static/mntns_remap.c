#define _GNU_SOURCE
#include <sched.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <limits.h>
#include <signal.h>
#include <sys/wait.h>

#include "zdtmtst.h"

const char *test_doc	= "Check two mounts in the same directory";
const char *test_author	= "Andrew Vagin <avagin@parallels.com>";

char *dirname;
TEST_OPTION(dirname, string, "directory name", 1);

int main(int argc, char **argv)
{
	task_waiter_t t;
	pid_t pid;

	test_init(argc, argv);

	task_waiter_init(&t);

	pid = fork();
	if (pid < 0)
		return -1;

	if (pid == 0) {
		if (unshare(CLONE_NEWNS))
			return 1;
		if (mount("/", "/", NULL, MS_SLAVE, NULL))
			return 1;
		if (mount("/", "/", NULL, MS_BIND, NULL))
			return 1;
		chdir("/");
		task_waiter_complete_current(&t);
		test_waitsig();
		return 0;
	}

	task_waiter_wait4(&t, pid);
	test_daemon();
	test_waitsig();

	kill(pid, SIGTERM);
	wait(NULL);

	pass();

	return 0;
}
