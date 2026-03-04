/*
 * bp_bench_worker.c - Multi-threaded worker for breakpoint benchmarking
 *
 * Spawns a child process which then spawns N threads that sleep forever.
 * The parent process waits until all threads in the child are started
 * and have acknowledged their start. The child process writes its PID
 * to a file and then sleeps. This process is meant to be checkpointed
 * and restored by the benchmark driver script.
 *
 * Usage: ./bp_bench_worker <num_threads> <pid_file>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

static int pipefd[2];
static void *thread_func(void *arg)
{
	/* Signal parent that we are ready */
	if (write(pipefd[1], "1", 1) != 1) {
		perror("write pipe");
		close(pipefd[1]);
		*((int *) 0) = 1; // bug
	}

	while (1)
		sleep(1000);
	return NULL;
}

int main(int argc, char **argv)
{
	int nr_threads;
	const char *pid_file;
	FILE *f;
	int i;
	pid_t pid;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <num_threads> <pid_file>\n",
			argv[0]);
		return 1;
	}

	nr_threads = atoi(argv[1]);
	pid_file = argv[2];

	if (nr_threads < 0 || nr_threads > 100000) {
		fprintf(stderr, "num_threads must be 0..100000\n");
		return 1;
	}

	if (pipe(pipefd) < 0) {
		perror("pipe");
		return 1;
	}

	pid = fork();
	if (pid < 0) {
		perror("fork");
		return 1;
	}

	if (pid == 0) {
		/* Child process (test process) */
		close(pipefd[0]);

		for (i = 0; i < nr_threads; i++) {
			pthread_t th;
			int ret = pthread_create(&th, NULL, thread_func, NULL);

			if (ret) {
				fprintf(stderr, "pthread_create #%d: %s\n",
					i, strerror(ret));
				return 1;
			}
			pthread_detach(th);
		}

		/* Write PID so the driver script can find us */
		f = fopen(pid_file, "w");
		if (!f) {
			perror("fopen pid_file");
			return 1;
		}
		fprintf(f, "%d\n", getpid());
		fclose(f);

		/* Signal parent that we are ready */
		if (write(pipefd[1], "1", 1) != 1) {
			perror("write pipe");
			return 1;
		}

		/* Sleep until killed or checkpointed */
		while (1)
			sleep(1000);
	} else {
		/* Parent process */
		char buf;
		close(pipefd[1]);
		for (i = 0; i < nr_threads + 1; i++) {
			if (read(pipefd[0], &buf, 1) != 1) {
				/* Child failed or exited prematurely */
				return 1;
			}
		}
		close(pipefd[0]);
		return 0;
	}

	return 0;
}
