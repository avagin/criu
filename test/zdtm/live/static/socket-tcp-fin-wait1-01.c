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

static int port = 8880;

#define TEST_MSG "Hello World!"
#define BUF_SIZE 4096

int main(int argc, char **argv)
{
	int fd, fd_s;
	pid_t extpid;
	int pfd[2];
	char cmd[4096], buf[BUF_SIZE];

#ifdef ZDTM_TCP_LOCAL
	test_init(argc, argv);
#endif

	if (pipe(pfd)) {
		err("pipe() failed");
		return 1;
	}

	extpid = fork();
	if (extpid < 0) {
		err("fork() failed");
		return 1;
	} else if (extpid == 0) {
		char c;

#ifndef ZDTM_TCP_LOCAL
		test_ext_init(argc, argv);
#endif

		close(pfd[1]);
		if (read(pfd[0], &port, sizeof(port)) != sizeof(port)) {
			err("Can't read port\n");
			return 1;
		}

		fd = tcp_init_client(ZDTM_FAMILY, "127.0.0.1", port);
		if (fd < 0)
			return 1;

		if (read(fd, &c, 1) != 0) {
			err("read");
			return 1;
		}

		if (write(fd, TEST_MSG, sizeof(TEST_MSG)) != sizeof(TEST_MSG)) {
			err("write");
			return 1;
		}

		close(fd);

		return 0;
	}

#ifndef ZDTM_TCP_LOCAL
	test_init(argc, argv);
#endif

	if ((fd_s = tcp_init_server(ZDTM_FAMILY, &port)) < 0) {
		err("initializing server failed");
		return 1;
	}

	close(pfd[0]);
	if (write(pfd[1], &port, sizeof(port)) != sizeof(port)) {
		err("Can't send port");
		return 1;
	}
	close(pfd[1]);

	/*
	 * parent is server of TCP connection
	 */
	fd = tcp_accept_server(fd_s);
	if (fd < 0) {
		err("can't accept client connection %m");
		return 1;
	}

	snprintf(cmd, sizeof(cmd), "iptables -A OUTPUT -p tcp --dport %d -j DROP", port);
	if (system(cmd))
		return 1;

	if (shutdown(fd, SHUT_WR) == -1) {
		err("shutdown");
		goto err;
	}

	test_daemon();
	test_waitsig();

	snprintf(cmd, sizeof(cmd), "iptables -D OUTPUT -p tcp --dport %d -j DROP", port);
	if (system(cmd))
		return 1;

	if (read(fd, buf, sizeof(buf)) != sizeof(TEST_MSG) ||
	    strncmp(buf, TEST_MSG, sizeof(TEST_MSG))) {
		err("read");
		return 1;
	}

	pass();
	return 0;
err:
	snprintf(cmd, sizeof(cmd), "iptables -D OUTPUT -p tcp --dport %d -j DROP", port);
	system(cmd);
	return 1;
}
