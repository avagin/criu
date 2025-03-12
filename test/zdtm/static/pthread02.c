/*
 * A simple testee program with threads
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <pthread.h>

#include "zdtmtst.h"

const char *test_doc = "Create a thread with a dead leader\n";
const char *test_author = "Andrew Vagin <avagin@openvz.org";

static void *thread_func(void *args)
{
	test_waitsig();
	pass();
	exit(0);
}

#define NR 128
int main(int argc, char *argv[])
{
	pthread_t th[NR];
	int ret, i;

	test_init(argc, argv);

	for (i = 0; i < NR; i++) {
		ret = pthread_create(&th[i], NULL, &thread_func, NULL);
		if (ret) {
			fail("Can't pthread_create");
			exit(1);
		}
	}

	test_daemon();
	test_waitsig();

	return 0;
}
