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

#define MEM_SIZE (1L << 29)

const char *test_doc = "Test big mappings";
const char *test_author = "Andrew Vagin <avagin@openvz.org";

int main(int argc, char **argv)
{
	char *m;
	long i;

	test_init(argc, argv);

	m = mmap(NULL, MEM_SIZE, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (m == MAP_FAILED) {
		fail();
		return 1;
	}

	for (i = 0; i < MEM_SIZE; i += 4096)
		m[i] = 1;

	test_daemon();
	test_wait_pre_dump();
	for (i = 0; i < MEM_SIZE; i += 4096 * 2)
		m[i] = 2;
	test_wait_pre_dump_ack();
	test_waitsig();

	pass();

	return 0;
}
