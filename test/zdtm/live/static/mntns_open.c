#define _GNU_SOURCE
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sched.h>
#include <sys/wait.h>
#include <stdlib.h>

#include "zdtmtst.h"

#ifndef CLONE_NEWNS
#define CLONE_NEWNS     0x00020000
#endif

const char *test_doc	= "Check that mnt_id is repsected";
const char *test_author	= "Pavel Emelianov <xemul@parallels.com>";

#define MPTS_ROOT	"/zdtm_mpts/"
#define MPTS_FILE	"F"

static char buf[1024];

#define NS_STACK_SIZE	4096
/* All arguments should be above stack, because it grows down */
struct ns_exec_args {
	char stack[NS_STACK_SIZE];
	char stack_ptr[0];
	int fd;
};

#define AWK_OK		13
#define AWK_FAIL	42
#define AWK_BS		57

#define _S(X)	#X
#define S(X)	_S(X)

int ns_child(void *_arg)
{
	struct ns_exec_args *args = _arg;
	int fd2, ret;
	char *cmd_pat = "cat /proc/self/fdinfo/%d /proc/self/fdinfo/%d |"
			" awk 'BEGIN{p=0}/mnt_id:/{"
			" if (p == 0) { p = $2; } "
			" else if (p != $2) { exit " S(AWK_OK) " } "
			" else { exit " S(AWK_FAIL) " } }'";

	fd2 = open(MPTS_ROOT MPTS_FILE, O_RDWR);
	test_waitsig();

	sprintf(buf, cmd_pat, args->fd, fd2);
	ret = system(buf);
	close(fd2);

	if (WIFEXITED(ret))
		ret = WEXITSTATUS(ret);
	else
		ret = AWK_BS;

	test_msg("AWK exited %d\n", ret);
	exit(ret);
}

static int test_fn(int argc, char **argv)
{
	FILE *f;
	unsigned fs_cnt, fs_cnt_last = 0;
	struct ns_exec_args args;
	bool private = false;
	pid_t pid = -1;

	if (!getenv("ZDTM_REEXEC")) {
		setenv("ZDTM_REEXEC", "1", 0);
		return execv(argv[0], argv);
	}

	test_init(argc, argv);
	close(0); /* /dev/null */
again:
	fs_cnt = 0;
	f = fopen("/proc/self/mountinfo", "r");
	if (!f) {
		fail("Can't open mountinfo");
		return -1;
	}

	if (mount(NULL, "/", NULL, MS_REC|MS_PRIVATE, NULL)) {
		err("Can't remount / with MS_PRIVATE");
		return -1;
	}

	while (fgets(buf, sizeof(buf), f) != NULL) {
		char *mp = buf, *end;

		mp = strchr(mp, ' ') + 1;
		mp = strchr(mp, ' ') + 1;
		mp = strchr(mp, ' ') + 1;
		mp = strchr(mp, ' ') + 1;
		end = strchr(mp, ' ');
		*end = '\0';

		if (!strcmp(mp, "/"))
			continue;
		if (!strcmp(mp, "/proc"))
			continue;

		if (umount(mp))
			test_msg("umount(`%s') failed: %m\n", mp);

		fs_cnt++;
	}

	fclose(f);

	if (!private) {
		private = true;
		goto again;
	}

	if (fs_cnt == 0)
		goto done;

	if (fs_cnt != fs_cnt_last) {
		fs_cnt_last = fs_cnt;
		goto again;
	}

	fail("Can't umount all the filesystems");
	return -1;

done:
	rmdir(MPTS_ROOT);
	if (mkdir(MPTS_ROOT, 0600) < 0) {
		fail("Can't make zdtm_sys");
		return 1;
	}

	if (getenv("ZDTM_NOSUBNS") == NULL) {
		args.fd = open(MPTS_ROOT MPTS_FILE, O_CREAT | O_RDWR, 0600);
		if (args.fd < 0) {
			fail("Can't open file");
			return 1;
		}

		pid = clone(ns_child, args.stack_ptr, CLONE_NEWNS | SIGCHLD, &args);
		if (pid < 0) {
			err("Unable to fork child");
			return 1;
		}

		close(args.fd);
	}

	test_daemon();
	test_waitsig();


	if (pid > 0) {
		kill(pid, SIGTERM);
		int status = 1;
		wait(&status);
		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) == AWK_OK)
				pass();
			else if (WEXITSTATUS(status) == AWK_FAIL)
				fail("Mount ID not restored");
			else
				fail("Failed to check mount IDs (%d)", WEXITSTATUS(status));
		} else
			fail("Test died");
	}

	unlink(MPTS_ROOT MPTS_FILE);
	rmdir(MPTS_ROOT);
	return 0;
}

int main(int argc, char **argv)
{
	if (getenv("ZDTM_REEXEC"))
		return test_fn(argc, argv);

	test_init_ns(argc, argv, CLONE_NEWNS, test_fn);
	return 0;
}
