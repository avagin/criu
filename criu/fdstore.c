#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

#include "common/scm.h"
#include "servicefd.h"
#include "fdstore.h"
#include "xmalloc.h"
#include "log.h"

static int next_id;

int fdstore_init(void)
{
	struct sockaddr_un addr;
	unsigned int addrlen;
	struct stat st;
	int sk, ret;

	sk = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	if (sk < 0) {
		pr_perror("Unable to create a socket");
		return -1;
	}

	if (fstat(sk, &st)) {
		pr_perror("Unable to stat a file descriptor");
		close(sk);
		return -1;
	}

	addr.sun_family = AF_UNIX;
	addrlen = snprintf(addr.sun_path, sizeof(addr.sun_path), "X/criu-fdstore-%"PRIx64, st.st_ino);
	addrlen += sizeof(addr.sun_family);

	addr.sun_path[0] = 0;

	/*
	 * This socket is connected to itself, so all messages are queued to
	 * its receive queue. Here we are going to use this socket to store
	 * file descriptors. For that we need to send a file descriptor in
	 * a queue and remeber its sequence number. Then we can set SO_PEEK_OFF
	 * to get a file descriptor without dequeuing it.
	 */
	if (bind(sk, (struct sockaddr *) &addr, addrlen)) {
		pr_perror("Unable to bind a socket");
		close(sk);
		return -1;
	}
	if (connect(sk, (struct sockaddr *) &addr, addrlen)) {
		pr_perror("Unable to connect a socket");
		close(sk);
		return -1;
	}

	ret = install_service_fd(FDSTORE_SK_OFF, sk);
	close(sk);
	if (ret < 0)
		return -1;

	return 0;
}

int fdstore_add(int fd)
{
	int sk = get_service_fd(FDSTORE_SK_OFF);

	if (send_fd(sk, NULL, 0, fd))
		return -1;

	next_id++;

	return next_id - 1;
}

int fdstore_get(int id)
{
	int sk = get_service_fd(FDSTORE_SK_OFF);
	int fd;

	if (setsockopt(sk, SOL_SOCKET, SO_PEEK_OFF, &id, sizeof(id))) {
		pr_perror("Unable to a peek offset");
		return -1;
	}

	if (__recv_fds(sk, &fd, 1, NULL, 0, MSG_PEEK) < 0) {
		pr_perror("Unable to get a file descriptor with the %d id", id);
		return -1;
	}
	return fd;
}

int fdstore_self_test()
{
	int fd, ids[2], i;
	char buf[10];

	pr_info("=== Check the fdstore subsytem ===\n");
	if (fdstore_init())
		return -1;

	fd = open("/dev/null", O_RDWR);
	if (fd < 0) {
		pr_perror("Unable to open /dev/null");
		return -1;
	}

	ids[0] = fdstore_add(fd);
	if (ids[0] < 0)
		return -1;

	close(fd);
	pr_info("Add /dev/null -> %d\n", ids[0]);

	fd = open("/dev/zero", O_RDONLY);
	if (fd < 0) {
		pr_perror("Unable to open /dev/zero");
		return -1;
	}

	ids[1] = fdstore_add(fd);
	if (ids[1] < 0)
		return -1;
	pr_info("Add /dev/zero -> %d\n", ids[1]);

	for (i = 2; ; i++) {
		if (fdstore_add(fd) < 0)
			break;
	}
	pr_info("The capacity of fdstore is %d descriptors\n", i);
	close(fd);

	fd = fdstore_get(ids[1]);
	if (fd < 0)
		return -1;
	close(fd);
	fd = fdstore_get(ids[0]);
	if (fd < 0)
		return -1;
	close(fd);
	fd = fdstore_get(ids[1]);
	if (fd < 0)
		return -1;
	close(fd);

	fd = fdstore_get(ids[1]);
	if (fd < 0)
		return -1;

	if (read(fd, buf, sizeof(buf)) != sizeof(buf)) {
		pr_perror("Unable to read /dev/zero");
		return -1;
	}
	close(fd);

	fd = fdstore_get(ids[0]);
	if (fd < 0)
		return -1;

	if (read(fd, buf, sizeof(buf)) != 0) {
		pr_perror("Unable to read /dev/null");
		return -1;
	}
	close(fd);

	return 0;
}
