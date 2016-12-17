#define _GNU_SOURCE
#include <sched.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <linux/if.h>

#include <netinet/ether.h>
#include <netlink/netlink.h>
#include <netlink/route/link.h>

#include "zdtmtst.h"

const char *test_doc	= "Check dump and restore a few network namespaces";

int main(int argc, char **argv)
{
	task_waiter_t lock;
	pid_t pid[2];
	int status = -1, ret, i;
        struct rtnl_link *link = NULL, *new;
	struct nl_sock *sk;
	int has_index = 1;

	test_init(argc, argv);
	task_waiter_init(&lock);

	for (i = 0; i < 2; i++) {
		pid[i] = fork();
		if (pid[i] < 0) {
			pr_perror("fork");
			return -1;
		}
		if (pid[i] == 0) {
			unshare(CLONE_NEWNET);

			task_waiter_complete(&lock, i);
			test_waitsig();

			return 0;
		}
		task_waiter_wait4(&lock, i);
	}

	sk = nl_socket_alloc();
	if (sk == NULL)
		return -1;

	ret = nl_connect(sk, NETLINK_ROUTE);
	if (ret < 0) {
		nl_socket_free(sk);
		pr_err("Unable to connect socket: %s", nl_geterror(ret));
		return -1;
	}

	for (i = 0; i < 2; i++) {
		char cmd[4096];

		snprintf(cmd, sizeof(cmd), "ip link add name zdtm%d index %d netns %d type veth peer name zdtm%d index %d", i, i * 10 + 12, pid[i], i, i * 10 + 12);
		if (system(cmd)) {
			has_index = 0;
			snprintf(cmd, sizeof(cmd), "ip link add name zdtm%d netns %d type veth peer name zdtm%d", i, pid[i], i);
			if (system(cmd))
				return 1;
		}
	}

	test_daemon();
	test_waitsig();

	for (i = 0; i < 2; i++) {
		link = rtnl_link_alloc();
		new = rtnl_link_alloc();
		if (has_index)
			rtnl_link_set_ifindex(link, i * 10 + 12);
		else {
			char name[43];
			snprintf(name, sizeof(name), "zdtm%d", i);
			rtnl_link_set_name(link, name);
			rtnl_link_set_name(new, name);
		}
		rtnl_link_set_flags(new, IFF_UP);
		ret = rtnl_link_change(sk, link, new, 0);
		if (ret) {
			fail("Unable to up the link: %s", nl_geterror(ret));
			return 1;
		}
	}

	for (i = 0; i < 2; i++) {
		kill(pid[i], SIGTERM);
		waitpid(pid[i], &status, 0);
		if (status) {
			fail();
			return 1;
		}
	}

	pass();
	return 0;
}
