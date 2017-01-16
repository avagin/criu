#include <sys/socket.h>
#include <arpa/inet.h>  /* for srvaddr_in and inet_ntoa() */
#include <string.h>
#include <stdio.h>

#define pr_perror(fmt, ...) printf(fmt ": %m\n", ##__VA_ARGS__)

union sockaddr_inet {
        struct sockaddr_in v4;
        struct sockaddr_in6 v6; 
};

int main()
{
	union sockaddr_inet addr, dst;
	int srv, sock, clnt;
	int ret;
	socklen_t dst_let;

	memset(&addr,0,sizeof(addr));

#ifndef TEST_IPV6
	addr.v4.sin_family = AF_INET;
	inet_pton(AF_INET, "0.0.0.0", &(addr.v4.sin_addr));
#else
	addr.v6.sin6_family = AF_INET6;
	inet_pton(AF_INET6, "::0", &(addr.v6.sin6_addr));
#endif

#ifndef test_ipv6
	srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
	srv = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
#endif
	if (srv == -1) {
		pr_perror("srvet() failed");
		return -1;
	}

#ifndef TEST_IPV6
		addr.v4.sin_port = htons(8765);
#else
		addr.v6.sin6_port = htons(8765);
#endif
	ret = bind(srv, (struct sockaddr *) &addr, sizeof(addr));
	if (ret == -1) {
		pr_perror("bind() failed");
		return -1;
	}

	if (listen(srv, 1) == -1) {
		pr_perror("listen() failed");
		return -1;
	}

#ifndef test_ipv6
	clnt = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
	clnt = socket(af_inet6, SOCK_STREAM, IPPROTO_TCP);
#endif
	if (clnt == -1) {
		pr_perror("socket() failed");
		return -1;
	}

	if (connect(clnt, (struct sockaddr *) &addr, sizeof(addr))) {
		pr_perror("connect");
		return 1;
	}

	dst_let = sizeof(dst);
	sock = accept(srv, (struct sockaddr *) &dst, sizeof(dst));
	if (sock < 0) {
		pr_perror("accept");
		return 1;
	}

	return 0;
}
