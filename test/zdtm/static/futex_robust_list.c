#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>

#include <linux/futex.h>
#include <linux/unistd.h>

#include <sys/mman.h>

#include "zdtmtst.h"

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) & ((TYPE *)0)->MEMBER)
#endif

const char *test_doc = "Check that robust futex in a MAP_SHARED file does not alter file content on process exit during dump";
const char *test_author = "Andrei Vagin";

struct lock_struct {
	_Atomic(unsigned int)	futex;
	struct robust_list	list;
};


char *filename;
TEST_OPTION(filename, string, "file name", 1);

static int set_robust_list(struct robust_list_head *head, size_t len)
{
	return syscall(__NR_set_robust_list, head, len);
}

static int set_list(struct robust_list_head *head)
{
	int ret;

	ret = set_robust_list(head, sizeof(*head));
	if (ret)
		return ret;

	head->futex_offset = (size_t)offsetof(struct lock_struct, futex) -
			     (size_t)offsetof(struct lock_struct, list);
	head->list.next = &head->list;
	head->list_op_pending = NULL;

	return 0;
}


int main(int argc, char **argv)
{
	struct lock_struct *lock;
	_Atomic(unsigned int) *futex, v;
	struct robust_list_head head;
	pid_t tid;
	int fd;

	test_init(argc, argv);

	fd = open(filename, O_CREAT | O_RDWR, 0600);
	if (fd == -1) {
		pr_perror("open");
		return 1;
	}
	if (ftruncate(fd, PAGE_SIZE)) {
		pr_perror("ftruncate");
		return 1;
	}

	lock = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, fd, 0);
	if (lock == MAP_FAILED) {
		pr_perror("mmap");
		return 1;
	}

	futex = &lock->futex;

	tid = gettid();
	*futex = tid | FUTEX_WAITERS;
	if (set_list(&head)) {
		pr_perror("set_list");
		return 1;
	}

	head.list_op_pending = &lock->list;

	test_daemon();
	test_waitsig();

	v = *futex;
	if (v != (tid | FUTEX_WAITERS)) {
		fail("unexpected value: %x (expected %x)\n", v, tid | FUTEX_WAITERS);
		return 1;
	}

	munmap(lock, PAGE_SIZE);
	close(fd);

	pass();
	return 0;
}
