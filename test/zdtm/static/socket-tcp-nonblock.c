
#include "zdtmtst.h"
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>  /* for sockaddr_in and inet_ntoa() */

#ifdef ZDTM_IPV6
#define ZDTM_FAMILY AF_INET6
#else
#define ZDTM_FAMILY AF_INET
#endif

const char *test_doc = "Check, that a TCP connection can be restored\n";
const char *test_author = "Andrey Vagin <avagin@parallels.com";

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <sched.h>
#include <netinet/tcp.h>

static int port = 8880;

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

union sockaddr_inet {
	struct sockaddr_in v4;
	struct sockaddr_in6 v6;
};

int tcp_init_client2(int family, char *servIP, unsigned short servPort)
{
	int sock;
	union sockaddr_inet servAddr;

	if ((sock = socket(family, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP)) < 0) {
		pr_perror("can't create socket");
		return -1;
	}
	/* Construct the server address structure */
	memset(&servAddr, 0, sizeof(servAddr));
	if (family == AF_INET) {
		servAddr.v4.sin_family      = AF_INET;
		servAddr.v4.sin_port        = htons(servPort + 100);
		inet_pton(AF_INET, servIP, &servAddr.v4.sin_addr);
	} else {
		servAddr.v6.sin6_family      = AF_INET6;
		servAddr.v6.sin6_port        = htons(servPort + 100);
		inet_pton(AF_INET6, servIP, &servAddr.v6.sin6_addr);
	}
	if (bind(sock, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) {
		pr_perror("can't connect to server");
		return -1;
	}

	memset(&servAddr, 0, sizeof(servAddr));
	if (family == AF_INET) {
		servAddr.v4.sin_family      = AF_INET;
		servAddr.v4.sin_port        = htons(servPort);
		inet_pton(AF_INET, servIP, &servAddr.v4.sin_addr);
	} else {
		servAddr.v6.sin6_family      = AF_INET6;
		servAddr.v6.sin6_port        = htons(servPort);
		inet_pton(AF_INET6, servIP, &servAddr.v6.sin6_addr);
	}
	if (connect(sock, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) {
		pr_perror("can't connect to server");
		return sock;
	}
	return sock;
}
int main(int argc, char **argv)
{
	int fd, fd_s, clt;
	char cmd[4096];

	test_init(argc, argv);

	if ((fd_s = tcp_init_server(ZDTM_FAMILY, &port)) < 0) {
		pr_err("initializing server failed\n");
		return 1;
	}


	clt = tcp_init_client2(ZDTM_FAMILY, "localhost", port);
	if (clt < 0) {
		pr_perror("Unable to create a client socket");
	        return 1;
	}

	/*
	* parent is server of TCP connection
	*/
	fd = tcp_accept_server(fd_s);
	if (fd < 0) {
		pr_err("can't accept client connection\n");
		return 1;
	}
	snprintf(cmd, sizeof(cmd), "iptables -w -t filter --protocol tcp -A INPUT --dport %d -j DROP", port + 100);
	if (system(cmd))
		return -1;
	write(fd, "asd", 3);
	close(clt);
	snprintf(cmd, sizeof(cmd), "iptables -w -t filter --protocol tcp -D INPUT --dport %d -j DROP", port + 100);
	if (system(cmd))
		return -1;

	test_daemon();
	test_waitsig();


	pass();
	return 0;
}
