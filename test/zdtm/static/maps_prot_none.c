#include <stdint.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "zdtmtst.h"

const char *test_doc = "Test a huge PROT_NONE mapping that requires "
		       "to read more than MAX_RW_COUNT from the pagemap file.";
const char *test_author = "Bui Quang Minh <minhquangbui99@gmail.com>";

#define MEM_SIZE (2UL << 40)

int main(int argc, char **argv)
{
	void *addr;

	test_init(argc, argv);

	addr = mmap(NULL, MEM_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, 0, 0);
	if (addr == MAP_FAILED) {
		pr_perror("Map failed");
		return 1;
	}

	test_daemon();
	test_waitsig();

	pass();
	return 1;
}
