#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>


#include "zdtmtst.h"

const char *test_doc	= "Check that environment didn't change";
const char *test_author	= "Pavel Emelianov <xemul@parallels.com>";

char *envname;
TEST_OPTION(envname, string, "environment variable name", 1);

static void sigh(int signo) {
}

void *th(void *args)
{
	while (1) {
		sleep(1);
	}
}

int main(int argc, char **argv)
{
	pthread_t t;
	char *env;

	test_init(argc, argv);

	if (pthread_create(&t, NULL, th, NULL)) {
		pr_perror("pthread_create");
		exit(1);
	}
	if (setenv(envname, test_author, 1)) {
		pr_perror("Can't set env var \"%s\" to \"%s\"", envname, test_author);
		exit(1);
	}
	signal(SIGTRAP, sigh);

	test_daemon();
	test_waitsig();

	env = getenv(envname);
	if (!env) {
		fail("can't get env var \"%s\": %m\n", envname);
		goto out;
	}
	kill(getpid(), SIGTRAP);

	if (strcmp(env, test_author))
		fail("%s != %s\n", env, test_author);
	else
		pass();
out:
	return 0;
}
