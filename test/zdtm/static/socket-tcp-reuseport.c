#include "zdtmtst.h"

#ifdef ZDTM_IPV6
#define ZDTM_FAMILY AF_INET6
#else
#define ZDTM_FAMILY AF_INET
#endif

const char *test_doc = "Check a case when one port is shared between two listening sockets\n";
const char *test_author = "Andrey Vagin <avagin@parallels.com";

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <sched.h>
#include <poll.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <arpa/inet.h>  /* for sockaddr_in and inet_ntoa() */

#define BUF_SIZE 4096

int read_data(int fd, unsigned char *buf, int size)
{
	int cur = 0;
	int ret;
	while (cur != size) {
		ret = read(fd, buf + cur, size - cur);
		if (ret <= 0)
			return -1;
		cur += ret;
	}

	return 0;
}

int write_data(int fd, const unsigned char *buf, int size)
{
	int cur = 0;
	int ret;

	while (cur != size) {
		ret = write(fd, buf + cur, size - cur);
		if (ret <= 0)
			return -1;
		cur += ret;
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct zdtm_tcp_opts opts = { .reuseaddr = false,
					.reuseport = true,
					.flags = SOCK_NONBLOCK};
	struct pollfd pfds[2];
	unsigned char buf[BUF_SIZE];
	int port[2] = {8880, 8880};
	int fd = -1, fd_s[2], clt, i, j;
	socklen_t optlen;
	int no = 0, val;
	uint32_t crc;

	test_init(argc, argv);

	for (i = 0; i < 2; i++) {
		if ((fd_s[i] = tcp_init_server_with_opts(ZDTM_FAMILY, &port[i], &opts)) < 0) {
			pr_err("initializing server failed\n");
			return 1;
		}
	}

	if (port[0] != port[1])
		return 1;

	if (setsockopt(fd_s[0], SOL_SOCKET, SO_REUSEPORT, &no, sizeof(int)) == -1 ) {
		pr_perror("Unable to set SO_REUSEPORT");
		return -1;
	}

	clt = tcp_init_client(ZDTM_FAMILY, "localhost", port[0]);
	if (clt < 0)
		return 1;

	for (i = 0; i < 2; i++) {
		pfds[i].fd = fd_s[i];
		pfds[i].events = POLLIN;
		pfds[i].revents = 0;
	}

	if (poll(pfds, 2, 0) != 1) {
		pr_perror("poll");
		return 1;
	}

	for (i = 0; i < 2; i++) {
		if (!(pfds[i].revents & POLLIN))
			continue;

		fd = tcp_accept_server(fd_s[i]);
		if (fd < 0) {
			pr_err("can't accept client connection\n");
			return 1;
		}
		break;
	}
	if (fd < 0)
		return 1;

	test_daemon();
	test_waitsig();


	optlen = sizeof(val);
	if (getsockopt(fd_s[0], SOL_SOCKET, SO_REUSEPORT, &val, &optlen)) {
		pr_perror("getsockopt");
		return 1;
	}
	if (val == 1) {
		fail("SO_REUSEPORT is set for %d\n", fd);
		return 1;
	}
	optlen = sizeof(val);
	if (getsockopt(fd_s[1], SOL_SOCKET, SO_REUSEPORT, &val, &optlen)) {
		pr_perror("getsockopt");
		return 1;
	}
	if (val != 1) {
		fail("SO_REUSEPORT is not set for %d\n", fd);
		return 1;
	}

	for (j = 0; j < 2; j++) {
		crc = 0;
		datagen(buf, BUF_SIZE, &crc);
		if (write_data(fd, buf, BUF_SIZE)) {
			pr_perror("can't write");
			return 1;
		}

		memset(buf, 0, BUF_SIZE);
		if (read_data(clt, buf, BUF_SIZE)) {
			pr_perror("read less then have to");
			return 1;
		}
		crc = 0;
		if (datachk(buf, BUF_SIZE, &crc))
			return 2;

		close(clt);
		close(fd);

		if (i == 2)
			break;

		clt = tcp_init_client(ZDTM_FAMILY, "localhost", port[0]);
		if (clt < 0)
			return 1;

		for (i = 0; i < 2 - j; i++) {
			pfds[i].fd = fd_s[i];
			pfds[i].events = POLLIN;
			pfds[i].revents = 0;
		}

		if (poll(pfds, 2 - j, 0) != 1) {
			pr_perror("poll");
			return 1;
		}

		fd = -1;
		for (i = 0; i < 2 - j; i++) {
			if (!(pfds[i].revents & POLLIN))
				continue;
			fd = tcp_accept_server(fd_s[i]);
			if (fd < 0) {
				pr_err("can't accept client connection %d\n", i);
				return 1;
			}
			shutdown(fd_s[i], SHUT_RDWR);
			close(fd_s[i]);
			fd_s[0] = fd_s[(i + 1) % 2];
			break;
		}
		if (fd < 0)
			return 1;
	}

	close(fd);
	close(clt);

	pass();
	return 0;
}
