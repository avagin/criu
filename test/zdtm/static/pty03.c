#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include "zdtmtst.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/wait.h>

const char *test_doc	= "Check a non-opened control terminal";
const char *test_author	= "Andrey Vagin <avagin@openvz.org>";

static const char teststr[] = "ping\n";

int main(int argc, char *argv[])
{
	char buf[sizeof(teststr)];
	int master, slave, ret;
	char *slavename;
	pid_t pid;

	test_init(argc, argv);

	master = open("/dev/ptmx", O_RDWR);
	if (master == -1) {
		pr_perror("open(%s) failed", "/dev/ptmx");
		return 1;
	}

	grantpt(master);
	unlockpt(master);

	slavename = ptsname(master);
	slave = open(slavename, O_RDWR);
	if (slave == -1) {
		pr_perror("open(%s) failed", slavename);
		return 1;
	}

	if (ioctl(slave, TIOCSCTTY, 1)) {
		pr_perror("Can't set a controll terminal");
		return 1;
	}

	close(slave);

	pid = fork();
	if (pid < 0)
		return 1;

	if (pid == 0) {
		slave = open("/dev/tty", O_RDWR);
		if (slave == -1) {
			pr_perror("Can't open the controll terminal");
			return -1;
		}
		test_waitsig();
		return 0;
	}

	test_daemon();
	test_waitsig();

	kill(pid, SIGTERM);
	wait(NULL);

	slave = open("/dev/tty", O_RDWR);
	if (slave == -1) {
		pr_perror("Can't open the controll terminal");
		return -1;
	}

	signal(SIGHUP, SIG_IGN);

	ret = write(master, teststr, sizeof(teststr) - 1);
	if (ret != sizeof(teststr) - 1) {
		pr_perror("write(master) failed");
		return 1;
	}

	ret = read(slave, buf, sizeof(teststr) - 1);
	if (ret != sizeof(teststr) - 1) {
		pr_perror("read(slave1) failed");
		return 1;
	}

	if (strncmp(teststr, buf, sizeof(teststr) - 1)) {
		fail("data mismatch");
		return 1;
	}

	close(master);
	close(slave);

	pass();

	return 0;
}
