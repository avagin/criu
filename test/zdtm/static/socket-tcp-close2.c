#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <linux/types.h>
#include <signal.h>
#include <arpa/inet.h>

#include "zdtmtst.h"

const char *test_doc = "Check both dump and restore with tcp_close on TCP_CLOSE sockets";
const char *test_author = "Bui Quang Minh <minhquangbui99@gmail.com>";

static int port = 8880;

int main(int argc, char **argv)
{
	int fd_s, fd, client, bound_sk, bound_port;
	char c;

	test_init(argc, argv);
	signal(SIGPIPE, SIG_IGN);

	fd_s = tcp_init_server(AF_INET, &port);
	if (fd_s < 0) {
		pr_err("Server initializations failed\n");
		return 1;
	}

	client = tcp_init_client(AF_INET, "localhost", port);
	if (client < 0) {
		pr_err("Client initializations failed\n");
		return 1;
	}

	fd = tcp_accept_server(fd_s);
	if (fd < 0) {
		pr_err("Can't accept client\n");
		return 1;
	}

	shutdown(client, SHUT_WR);
	shutdown(fd, SHUT_WR);

	{
		struct sockaddr_in addr;
		socklen_t addrlen = sizeof(addr);
		int ret;

		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		inet_pton(AF_INET, "0.0.0.0", &(addr.sin_addr));
		bound_sk = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (bound_sk == -1) {
			pr_perror("socket() failed");
			return 1;
		}
		ret = bind(bound_sk, (struct sockaddr *)&addr, sizeof(addr));
		if (ret == -1) {
			pr_perror("bind");
			return 1;
		}
		getsockname(bound_sk, (struct sockaddr *)&addr, &addrlen);
		bound_port = ntohs(addr.sin_port);
	}

	test_daemon();
	test_waitsig();

	if (read(fd, &c, 1) != 0) {
		fail("read server");
		return 1;
	}
	if (read(client, &c, 1) != 0) {
		fail("read client");
		return 1;
	}
	if (write(client, &c, 1) != -1) {
		fail("write client");
		return 1;
	}
	if (write(fd, &c, 1) != -1) {
		fail("write server");
		return 1;
	}

	{
		struct sockaddr_in addr;
		socklen_t addrlen = sizeof(addr);

		getsockname(bound_sk, (struct sockaddr *)&addr, &addrlen);
		if (ntohs(addr.sin_port) != bound_port) {
			fail("port hasn't been restored: %d != %d",
				bound_port, ntohs(addr.sin_port));
			return 1;
		}
		addr.sin_port = htons(port);
		inet_pton(AF_INET, "localhost", &addr.sin_addr);
		if (connect(bound_sk, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			pr_perror("can't connect to server");
			return 1;
		}
		fd = tcp_accept_server(fd_s);
		if (fd < 0) {
			pr_err("Can't accept client\n");
			return 1;
		}
	}

	pass();
	return 0;
}
