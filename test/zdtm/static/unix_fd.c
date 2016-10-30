#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#include "zdtmtst.h"

union fdmsg {
	struct cmsghdr h;
	char buf[CMSG_SPACE(sizeof(int))];
};

/* Tested to work on perl 5.005_03
 *   Linux-2.2.14 glibc-2.0.7 (libc.so.6) i586  BSD4.4
 *   Linux-2.0.38 glibc-2.0.7 (libc.so.6) i586  BSD4.4
 *   SunOS-5.6, gcc-2.7.2.3, Sparc BSD4.3
 * see also: linux/net/unix/af_unix.c
 */


int
sendfd(sock_fd, send_me_fd)
	int sock_fd;
	int send_me_fd;
{
	int ret = 0;
	struct iovec  iov[1];
	struct msghdr msg;

	iov[0].iov_base = &ret;  /* Don't send any data. Note: der Mouse
				  * <mouse@Rodents.Montreal.QC.CA> says
				  * that might work better if at least one
				  * byte is sent. */
	iov[0].iov_len  = 1;

	msg.msg_iov     = iov;
	msg.msg_iovlen  = 1;
	msg.msg_name    = 0;
	msg.msg_namelen = 0;

	{
		/* New BSD 4.4 way (ouch, why does this have to be
		 * so convoluted). */

		union  fdmsg  cmsg;
		struct cmsghdr* h;
		int *pfd;

		msg.msg_control = cmsg.buf;
		msg.msg_controllen = sizeof(union fdmsg);
		msg.msg_flags = 0;

		h = CMSG_FIRSTHDR(&msg);
		h->cmsg_len   = CMSG_LEN(sizeof(int));
		h->cmsg_level = SOL_SOCKET;
		h->cmsg_type  = SCM_RIGHTS;
		pfd = (int*)CMSG_DATA(h);
		*(pfd) = send_me_fd;

		if (sendmsg(sock_fd, &msg, 0) < 0) {
			ret = 0;
		} else {
			ret = 1;
		}
	}
	/*fprintf(stderr,"send %d %d %d %d\n",sock_fd, send_me_fd, ret, errno);*/
	return ret;
}

int
recvfd(sock_fd)
	int sock_fd;
{
	int count;
	int ret = 0;
	struct iovec  iov[1];
	struct msghdr msg;

	iov[0].iov_base = &ret;  /* don't receive any data */
	iov[0].iov_len  = 1;

	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;

	union fdmsg  cmsg;
	struct cmsghdr* h;
	int *pfd;

	msg.msg_control = cmsg.buf;
	msg.msg_controllen = sizeof(union fdmsg);
	msg.msg_flags = 0;

	h = CMSG_FIRSTHDR(&msg);
	h->cmsg_len   = CMSG_LEN(sizeof(int));
	h->cmsg_level = SOL_SOCKET;  /* Linux does not set these */
	h->cmsg_type  = SCM_RIGHTS;  /* upon return */
	pfd = (int*)CMSG_DATA(h);
	*(pfd) = -1;

	count = recvmsg(sock_fd, &msg, 0);
	if (count < 0) {
		pr_perror("recvmsg");
		return -1;
	}

	h = CMSG_FIRSTHDR(&msg);   /* can realloc? */
	if (   h == NULL
	    || h->cmsg_len    != CMSG_LEN(sizeof(int))
	    || h->cmsg_level  != SOL_SOCKET
	    || h->cmsg_type   != SCM_RIGHTS ) {
		/* This should really never happen */
		if (h)
			pr_err("protocol failure: %ld %d %d\n",
				h->cmsg_len, h->cmsg_level, h->cmsg_type);
		else
			pr_err("protocol failure: NULL cmsghdr*\n");
		return -1;
	}

	return *(pfd);
}

int main(int argc, char *argv[])
{
	int fd, err, new_fd;
	int sockpair[2];
	char buf[10];

	test_init(argc, argv);

	fd = open("/dev/zero", O_RDWR);
	if (fd < 0) {
		pr_perror("open");
		return -1;
	}

	err = socketpair(PF_LOCAL, SOCK_DGRAM, 0, sockpair);
	if (err) {
		pr_perror("socketpair");
		return -1;
	}

	if (!sendfd(sockpair[0], fd)) {
		pr_perror("sendfd");
		return -1;
	}

	test_daemon();
	test_waitsig();

	new_fd = recvfd(sockpair[1]);
	if (new_fd < 0)
		return 1;

	if (read(new_fd, buf, 10) != 10) {
		fail("Failed: can read from SCM_RIGHTS descriptor\n");
		return 1;
	}

	pass();
	return 0;
}
