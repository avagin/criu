#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>

#include "zdtmtst.h"

const char *test_doc	= "Check that environment didn't change";
const char *test_author	= "Pavel Emelianov <xemul@parallels.com>";

#define NPROC 1000

int main(int argc, char **argv)
{
	int i;
	pid_t pid[NPROC];

	test_init(argc, argv);

	for (i = 0; i < NPROC; i++) {
		pid[i] = fork();
		if (pid[i] == 0) {
			test_waitsig();
			return 0;
		}
		if (pid[i] < 0)
			return 1;
	}


	test_daemon();
	test_waitsig();

	for (i = 0; i < NPROC; i++) {
		int status;
		kill(pid[i], SIGTERM);
		waitpid(pid[i], &status, 0);
	}
	pass();
	return 0;
}
