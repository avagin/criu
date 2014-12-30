#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include "zdtmtst.h"

#define MEM_SIZE (256L << 20)

const char *test_doc	= "Test big mappings";
const char *test_author	= "Andrew Vagin <avagin@openvz.org";

int main(int argc, char ** argv)
{
	void *m;
	int i, pid;

	test_init(argc, argv);

	m = mmap(NULL, MEM_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m == MAP_FAILED) {
		err("mmap");
		return 1;
	}

	for (i = 0; i < MEM_SIZE / PAGE_SIZE; i += 2)
		munmap(m + i * PAGE_SIZE, PAGE_SIZE);

	pid = fork();
	if (pid < 0)
		return 1;
	if (pid == 0) {
		test_waitsig();
		return 0;
	}

	test_daemon();
	test_waitsig();

	kill(pid, SIGTERM);
	wait(NULL);

	pass();

	return 0;
}
