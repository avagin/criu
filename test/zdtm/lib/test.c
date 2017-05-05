#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sched.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/prctl.h>
#include <grp.h>

#include "zdtmtst.h"
#include "lock.h"
#include "ns.h"

futex_t sig_received;

static void sig_hand(int signo)
{
	futex_set_and_wake(&sig_received, signo);
}

static char *outfile;
TEST_OPTION(outfile, string, "output file", 1);
char *pidfile;
TEST_OPTION(pidfile, string, "file to store pid", 1);

static pid_t master_pid = 0;

int test_fork_id(int id)
{
	return fork();
}

static int cwd = -1;

static void test_fini(void)
{
	char path[PATH_MAX];

	if (getpid() != master_pid)
		return;

	snprintf(path, sizeof(path), "%s%s", outfile, INPROGRESS);
	renameat(cwd, path, cwd, outfile);

	unlinkat(cwd, pidfile, 0);
}

static void setup_outfile()
{
	if (!access(outfile, F_OK) || errno != ENOENT) {
		fprintf(stderr, "Output file %s appears to exist, aborting\n",
			outfile);
		exit(1);
	}

	cwd = open(".", O_RDONLY);
	if (cwd < 0) {
		fprintf(stderr, "Unable to open\n");
		exit(1);
	}

	if (atexit(test_fini)) {
		fprintf(stderr, "Can't register exit function\n");
		exit(1);
	}
	if (test_log_init(outfile, INPROGRESS))
		exit(1);
}

static void redir_stdfds()
{
	int nullfd;

	nullfd = open("/dev/null", O_RDWR);
	if (nullfd < 0) {
		pr_perror("Can't open /dev/null");
		exit(1);
	}

	dup2(nullfd, STDIN_FILENO);
	if (nullfd > 2)
		close(nullfd);
}

void test_ext_init(int argc, char **argv)
{
	parseargs(argc, argv);
	if (test_log_init(outfile, ".external"))
		exit(1);
}

int write_pidfile(int pid)
{
	int fd = -1;
	char tmp[] = ".zdtm.pidfile.XXXXXX";

	fd = mkstemp(tmp);
	if (fd == -1) {
		fprintf(stderr, "Can't create the file %s: %m\n", tmp);
		return -1;
	}

	if (fchmod(fd, 0666) < 0) {
		fprintf(stderr, "Can't fchmod %s: %m\n", tmp);
		goto err_c;
	}

	if (dprintf(fd, "%d", pid) == -1) {
		fprintf(stderr, "Can't write in the file %s: %m\n", tmp);
		goto err_c;
	}

	close(fd);

	if (rename(tmp, pidfile) < 0) {
		fprintf(stderr, "Can't rename %s to %s: %m\n", tmp, pidfile);
		goto err_u;
	}

	return 0;

err_c:
	close(fd);
err_u:
	unlink(tmp);
	return -1;
}
static int ppipe[2];

void test_init(int argc, char **argv)
{
	pid_t pid;
	char *val;
	struct sigaction sa = {
		.sa_handler	= sig_hand,
		.sa_flags	= SA_RESTART,
	};
	sigemptyset(&sa.sa_mask);

	parseargs(argc, argv);

	val = getenv("ZDTM_NEWNS");
	if (val) {
		if (!strcmp(val, "1")) {
			ns_create(argc, argv);
			exit(1);
		}

		if (!strcmp(val, "2")) {
			test_log_init(outfile, "ns");
			redir_stdfds();
			ns_init(argc, argv);
		}
	}

	if (unshare(CLONE_NEWPID))
		exit(1);
	val = getenv("ZDTM_GROUPS");
	if (val) {
		char *tok = NULL;
		unsigned int size = 0, groups[NGROUPS_MAX];

		tok = strtok(val, " ");
		while (tok) {
			size++;
			groups[size - 1] = atoi(tok);
			tok = strtok(NULL, " ");
		}

		if (setgroups(size, groups)) {
			fprintf(stderr, "Can't set groups: %m");
			exit(1);
		}
	}

	val = getenv("ZDTM_GID");
	if (val && (setgid(atoi(val)) == -1)) {
		fprintf(stderr, "Can't set gid: %m");
		exit(1);
	}

	val = getenv("ZDTM_UID");
	if (val && (setuid(atoi(val)) == -1)) {
		fprintf(stderr, "Can't set gid: %m");
		exit(1);
	}

	if (prctl(PR_SET_DUMPABLE, 1)) {
		fprintf(stderr, "Can't set the dumpable flag");
		exit(1);
	}

	if (sigaction(SIGTERM, &sa, NULL)) {
		fprintf(stderr, "Can't set SIGTERM handler: %m\n");
		exit(1);
	}

	if (sigaction(SIGCHLD, &sa, NULL)) {
		fprintf(stderr, "Can't set SIGCHLD handler: %m\n");
		exit(1);
	}

	setup_outfile();
	redir_stdfds();

	if (pipe(ppipe))
		exit(1);

	pid = fork();
	if (pid < 0) {
		pr_perror("Daemonizing failed");
		exit(1);
	}

	if (pid) {	/* parent will exit when the child is ready */
		char c;
		close(ppipe[1]);

		read(ppipe[0], &c, sizeof(c));
		close(ppipe[0]);


		if (write_pidfile(pid))
			exit(1);

		_exit(0);
	}

	close(ppipe[0]);
	if (setsid() < 0) {
		pr_perror("Can't become session group leader");
		exit(1);
	}

	/* record the test pid to remember the ownership of the pidfile */
	master_pid = getpid();

	sa.sa_handler = SIG_DFL;
	if (sigaction(SIGCHLD, &sa, NULL)) {
		pr_perror("Can't reset SIGCHLD handler");
		exit(1);
	}

	srand48(time(NULL));	/* just in case we need it */
}

void test_daemon()
{
	char c = 0;

	write(ppipe[1], &c, 1);
	close(ppipe[1]);
	return;
}

int test_go(void)
{
	return !futex_get(&sig_received);
}

void test_waitsig(void)
{
	futex_wait_while(&sig_received, 0);
}
