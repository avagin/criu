#include "zdtmtst.h"

#ifdef ZDTM_IPV6
#define ZDTM_FAMILY AF_INET6
#else
#define ZDTM_FAMILY AF_INET
#endif

const char *test_doc = "Check sockets in TCP_FIN_WAIT* states\n";
const char *test_author = "Andrey Vagin <avagin@parallels.com";

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/wait.h>


static int port = 8880;

#define TEST_MSG "Hello World!"
#define BUF_SIZE 4096

int main(int argc, char **argv)
{
	int fd, fd_s, status;
	pid_t extpid, pid;
	char cmd[4096];

#ifdef ZDTM_TCP_LOCAL
	test_init(argc, argv);
#endif

	if ((fd_s = tcp_init_server(ZDTM_FAMILY, &port)) < 0) {
		err("initializing server failed");
		return 1;
	}

	extpid = fork();
	if (extpid < 0) {
		err("fork() failed");
		return 1;
	} else if (extpid == 0) {
#ifndef ZDTM_TCP_LOCAL
		test_ext_init(argc, argv);
#endif
		/*
		 * parent is server of TCP connection
		 */
		fd = tcp_accept_server(fd_s);
		if (fd < 0) {
			err("can't accept client connection %m");
			return 1;
		}


		return 0;
	}

	close(fd_s);

#ifndef ZDTM_TCP_LOCAL
	test_init(argc, argv);
#endif

	snprintf(cmd, sizeof(cmd), "iptables -A INPUT -p tcp --sport %d -j DROP", port);
	if (system(cmd))
		return 1;

	pid = fork();
	if (pid  < 0)
		goto err;

	if (pid == 0) {
		fd = tcp_init_client(ZDTM_FAMILY, "127.0.0.1", port);
		if (fd < 0)
			return 1;
		return 0;
	}

	test_daemon();
	test_waitsig();

	snprintf(cmd, sizeof(cmd), "iptables -D INPUT -p tcp --sport %d -j DROP", port);
	if (system(cmd))
		return 1;

	if (waitpid(pid, &status, 0) == -1) {
		err("waitpid");
		goto err;
	}

	if (status != 0)
		goto err;

	pass();
	return 0;
err:
	snprintf(cmd, sizeof(cmd), "iptables -D INPUT -p tcp --sport %d -j DROP", port);
	system(cmd);
	return 1;
}
