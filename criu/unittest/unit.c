#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "log.h"
#include "util.h"
#include "criu-log.h"
#include "bfd.h"
#include "compression.h"
#include "page.h"
#include "pagemap.h"
#include "cr_options.h"
#include "plugin.h"
#include "work-queue.h"

int parse_statement(int i, char *line, char **configuration);

static void test_plugin_options(void)
{
	bool usage_error = true;
	bool has_exec_cmd = false;
	char **plugin_argv;
	int plugin_argc;
	char *argv[] = {
		(char *)"criu",
		(char *)"--no-default-config",
		(char *)"--plugin-option",
		(char *)"example.option=first",
		(char *)"check",
		NULL,
	};
	assert(init_opts() == 0);
	assert(parse_options(5, argv, &usage_error, &has_exec_cmd, PARSING_GLOBAL_CONF) == 0);
	assert(criu_plugin_get_options(&plugin_argc, &plugin_argv) == 0);
	assert(plugin_argc == 2);
	assert(!strcmp(plugin_argv[0], "criu-plugin"));
	assert(!strcmp(plugin_argv[1], "--example.option=first"));
	assert(plugin_argv[2] == NULL);
	cr_plugin_options_clear();

	assert(cr_plugin_option_add_arg("example.option=first") == 0);
	assert(cr_plugin_option_add_arg("example.option=second") == 0);
	assert(cr_plugin_option_add_arg("example.empty=") == 0);
	assert(cr_plugin_option_add_arg("example.equals=left=right") == 0);
	assert(criu_plugin_get_options(&plugin_argc, &plugin_argv) == 0);
	assert(plugin_argc == 5);
	assert(!strcmp(plugin_argv[1], "--example.option=first"));
	assert(!strcmp(plugin_argv[2], "--example.option=second"));
	assert(!strcmp(plugin_argv[3], "--example.empty="));
	assert(!strcmp(plugin_argv[4], "--example.equals=left=right"));
	assert(plugin_argv[5] == NULL);
	cr_plugin_options_clear();

	assert(cr_plugin_option_add_arg("example.option=config") == 0);
	cr_plugin_default_options_parsed();
	assert(cr_plugin_option_add_arg("example.option=request") == 0);
	assert(criu_plugin_get_options(&plugin_argc, &plugin_argv) == 0);
	assert(plugin_argc == 3);
	assert(!strcmp(plugin_argv[1], "--example.option=config"));
	assert(!strcmp(plugin_argv[2], "--example.option=request"));

	cr_plugin_options_clear_request();
	assert(criu_plugin_get_options(&plugin_argc, &plugin_argv) == 0);
	assert(plugin_argc == 2);
	assert(!strcmp(plugin_argv[1], "--example.option=config"));
	cr_plugin_options_clear();

	assert(cr_plugin_option_add_arg("missing.option=value") == 0);
	assert(criu_plugin_get_options(&plugin_argc, &plugin_argv) == 0);
	assert(plugin_argc == 2);
	assert(!strcmp(plugin_argv[1], "--missing.option=value"));

	/* Plugin option without an explicit value (flag) */
	assert(cr_plugin_option_add_arg("example.option") == 0);
	assert(criu_plugin_get_options(&plugin_argc, &plugin_argv) == 0);
	assert(plugin_argc == 3);
	assert(!strcmp(plugin_argv[2], "--example.option"));

	assert(cr_plugin_option_add_arg("example") == -1);
	assert(cr_plugin_option_add_arg("example.") == -1);
	assert(cr_plugin_option_add_arg(".option") == -1);
	assert(cr_plugin_option_add_arg(".option=value") == -1);
	assert(cr_plugin_option_add_arg("example.=value") == -1);
	assert(cr_plugin_option_add_arg("--example.option") == -1);
	assert(cr_plugin_option_add_arg("-example.option") == -1);

	cr_plugin_options_clear();
	assert(criu_plugin_get_options(&plugin_argc, &plugin_argv) == 0);
	assert(plugin_argc == 1);
	assert(!strcmp(plugin_argv[0], "criu-plugin"));
	assert(plugin_argv[1] == NULL);
	assert(criu_plugin_get_options(NULL, &plugin_argv) == -EINVAL);
	assert(criu_plugin_get_options(&plugin_argc, NULL) == -EINVAL);

	/* Test that init_opts() resets plugin options */
	assert(cr_plugin_option_add_arg("example.option=saved") == 0);
	cr_plugin_default_options_parsed();
	assert(criu_plugin_get_options(&plugin_argc, &plugin_argv) == 0);
	assert(plugin_argc == 2);

	assert(init_opts() == 0);
	assert(criu_plugin_get_options(&plugin_argc, &plugin_argv) == 0);
	assert(plugin_argc == 1);
	assert(!strcmp(plugin_argv[0], "criu-plugin"));
	assert(plugin_argv[1] == NULL);

	cr_plugin_options_clear();
}

static void test_pagemap_offset_alignment(void)
{
	off_t aligned = (off_t)PAGE_SIZE * 3;

	assert(pagemap_page_align_offset(aligned) == aligned);
	assert(pagemap_page_align_offset(aligned + 1) == aligned + PAGE_SIZE);

	if (sizeof(off_t) > sizeof(uint32_t)) {
		off_t above_4g = ((off_t)1 << 32) + 1;

		assert(pagemap_page_align_offset(above_4g) ==
		       ((off_t)1 << 32) + PAGE_SIZE);
	}
}

static void test_bfd(void)
{
	struct bfd f;
	char *str;
	const int lines = 5;
	char *long_line[lines];
	int size = 1024 * 1024;
	int i, fd;

	fd = memfd_create("criu-bfd-test", 0);
	assert(fd >= 0);

	for (i = 0; i < lines; i++) {
		int j;

		long_line[i] = malloc(size + 2);
		assert(long_line[i]);
		long_line[i][0] = 'A' + (i % 26);
		for (j = 1; j < size; j++)
			long_line[i][j] = 'a' + (j % 26);
		long_line[i][size] = '\n';
		long_line[i][size + 1] = '\0';

		assert(write(fd, long_line[i], size + 1) == size + 1);
	}
	assert(lseek(fd, 0, SEEK_SET) == 0);

	f.fd = fd;
	assert(bfdopenr(&f) == 0);

	for (i = 0; i < lines; i++) {
		str = breadline(&f);
		assert(str);
		assert(strlen(str) == size);
		/* long_line has \n, str hasn't */
		assert(strcmp(str, long_line[i]) != 0);
		str[size] = '\n';
		assert(memcmp(str, long_line[i], size + 1) == 0);
	}

	bclose(&f);
	for (i = 0; i < lines; i++)
		free(long_line[i]);
}

static void test_bwrite(void)
{
	struct bfd f;
	char *buf;
	int size = 1024 * 1024;
	int i;
	int fd;
	char *read_buf;

	fd = memfd_create("criu-bfd-test", 0);
	assert(fd >= 0);

	buf = malloc(size);
	assert(buf);
	for (i = 0; i < size; i++)
		buf[i] = 'z' - (i % 26);

	f.fd = dup(fd);
	assert(f.fd >= 0);
	assert(bfdopenw(&f) == 0);

	assert(bwrite(&f, buf, size) == size);
	bclose(&f);

	assert(lseek(fd, 0, SEEK_SET) == 0);
	read_buf = malloc(size);
	assert(read_buf);
	assert(read(fd, read_buf, size) == size);
	assert(memcmp(buf, read_buf, size) == 0);

	close(fd);
	free(buf);
	free(read_buf);
}

#ifdef CONFIG_LZ4
static void test_compress_roundtrip(const char *page, int acceleration)
{
	char compressed[PAGE_COMPRESSED_SIZE_BOUND];
	char decompressed[PAGE_SIZE];
	int cs;

	cs = compress_block(page, 1, compressed,
			    PAGE_COMPRESSED_SIZE_BOUND, acceleration);
	assert(cs >= 0);
	assert(cs <= PAGE_COMPRESSED_SIZE_BOUND);
	assert(decompress_block(compressed, cs, 1, decompressed) == 0);
	assert(memcmp(page, decompressed, PAGE_SIZE) == 0);
}

static void test_compression(void)
{
	char cbuf[PAGE_COMPRESSED_SIZE_BOUND];
	char page[PAGE_SIZE];
	const int accels[] = { 1, 4, 100 };

	/* Zero-page detection */
	memset(page, 0, PAGE_SIZE);
	assert(page_is_all_zero(page) == true);
	page[PAGE_SIZE - 1] = 1;
	assert(page_is_all_zero(page) == false);
	page[PAGE_SIZE - 1] = 0;
	page[0] = 0x42;
	assert(page_is_all_zero(page) == false);


	for (int a = 0; a < 3; a++) {
		int accel = accels[a];
		int cs;

		/* Zero-filled page: should return 0 for zero-page block */
		memset(cbuf, 0, sizeof(cbuf));
		memset(page, 0, PAGE_SIZE);
		cs = compress_block(page, 1,
				    cbuf, PAGE_COMPRESSED_SIZE_BOUND, accel);
		assert(cs == 0);
		test_compress_roundtrip(page, accel);

		/* Repeating pattern */
		for (int i = 0; i < PAGE_SIZE; i++)
			page[i] = i & 0xff;
		test_compress_roundtrip(page, accel);

		/* Pseudo-random data (incompressible) */
		srand(42);
		for (int i = 0; i < PAGE_SIZE; i++)
			page[i] = rand() & 0xff;
		test_compress_roundtrip(page, accel);

		/* Single non-zero byte */
		memset(cbuf, 0, sizeof(cbuf));
		memset(page, 0, PAGE_SIZE);
		page[0] = 0x42;
		cs = compress_block(page, 1,
				    cbuf, PAGE_COMPRESSED_SIZE_BOUND, accel);
		assert(cs > 0 && cs < PAGE_SIZE);
		test_compress_roundtrip(page, accel);
	}
}

static void test_encoded_stream_zero_batch(void)
{
	uint32_t block_sizes[] = { 0, 0 };
	char pages[2 * PAGE_SIZE];
	size_t i;

	memset(pages, 0xa5, sizeof(pages));
	assert(encoded_stream_read_batch(-1, pages, block_sizes, 2, 0, 0, NULL, 0) == 0);
	for (i = 0; i < sizeof(pages); i++)
		assert(pages[i] == 0);
}

static unsigned int count_task_threads(void)
{
	struct dirent *entry;
	unsigned int nr_threads = 0;
	DIR *dir;

	dir = opendir("/proc/self/task");
	assert(dir);
	while ((entry = readdir(dir))) {
		if (entry->d_name[0] != '.')
			nr_threads++;
	}
	closedir(dir);
	return nr_threads;
}

static void wait_for_task_threads(unsigned int expected)
{
	unsigned int attempt;

	for (attempt = 0; attempt < 100; attempt++) {
		if (count_task_threads() == expected)
			return;
		usleep(1000);
	}
	assert(count_task_threads() == expected);
}

static void record_decompression_overlap(void *arg)
{
	unsigned int *calls = arg;

	(*calls)++;
}

static void test_parallel_decompression(void)
{
	struct cr_work_budget budget;
	const unsigned int pages_per_job = (512UL << 10) / PAGE_SIZE;
	const size_t nr_jobs = 8;
	const size_t job_bytes = (size_t)pages_per_job * PAGE_SIZE;
	const size_t src_size = nr_jobs * job_bytes;
	const size_t compressed_stride = BLOCK_COMPRESSED_SIZE_BOUND(pages_per_job);
	struct decompress_job *jobs;
	char *compressed;
	char *decompressed;
	char *src;
	struct cr_work_queue wq;
	unsigned int available_threads;
	unsigned int baseline_threads;
	unsigned int expected_threads;
	unsigned int overlap_calls = 0;
	size_t job;

	assert(cr_work_thread_limit(0, 64) == 64);
	assert(cr_work_thread_limit(1, 64) == 1);
	assert(cr_work_thread_limit(8, 64) == 8);
	assert(cr_work_thread_limit(100, 64) == 64);
	assert(cr_work_thread_limit(0, 0) == 1);

	cr_work_budget_init(&budget, 0);
	available_threads = budget.thread_capacity;
	assert(available_threads >= 1);
	{
		cpu_set_t affinity;

		CPU_ZERO(&affinity);
		if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0)
			assert(available_threads == (unsigned int)CPU_COUNT(&affinity));
	}
	cr_work_budget_init(&budget, 4);
	assert(budget.thread_capacity == min(available_threads, 4U));
	assert(budget.max_workers == min(available_threads, 4U) - 1);
	assert(futex_get(&budget.batches) == 2);
	/* Automatic and serial calls both leave CPUs available to sibling calls. */
	cr_work_budget_init(&budget, 0);
	assert(budget.thread_capacity == available_threads);
	cr_work_set_shared_budget(&budget);
	assert(cr_work_batch_try_acquire());
	assert(cr_work_batch_try_acquire());
	assert(!cr_work_batch_try_acquire());
	cr_work_batch_release();
	assert(cr_work_batch_try_acquire());
	cr_work_batch_release();
	cr_work_batch_release();
	assert(futex_get(&budget.batches) == 2);
	budget.thread_capacity = 1;
	assert(!compressed_restore_has_parallel_capacity(0));
	budget.thread_capacity = available_threads;
	assert(!compressed_restore_has_parallel_capacity(1));
	assert(compressed_restore_has_parallel_capacity(0) == (available_threads > 1));

	src = malloc(src_size);
	compressed = malloc(nr_jobs * compressed_stride);
	decompressed = malloc(src_size);
	jobs = malloc(nr_jobs * sizeof(*jobs));
	assert(src && compressed && decompressed && jobs);

	for (job = 0; job < nr_jobs; job++) {
		char *job_src = src + job * job_bytes;
		char *job_compressed = compressed + job * compressed_stride;
		int cs;
		size_t byte;

		for (byte = 0; byte < job_bytes; byte++)
			job_src[byte] = (char)(job + byte);
		cs = compress_block(job_src, pages_per_job, job_compressed, compressed_stride, 1);
		assert(cs > 0 && (size_t)cs < job_bytes);

		jobs[job].src = job_compressed;
		jobs[job].dst = decompressed + job * job_bytes;
		jobs[job].compressed_size = cs;
		jobs[job].pages = pages_per_job;
		jobs[job].block_index = job;
	}

	assert(cr_work_queue_init(&wq, &budget) == 0);
	baseline_threads = count_task_threads();
	assert(decompress_jobs_parallel(
		       &wq, jobs, 1, job_bytes, 1,
		       record_decompression_overlap, &overlap_calls) == 0);
	assert(memcmp(src, decompressed, job_bytes) == 0);
	assert(overlap_calls == 0);
	assert(wq.nr_workers == 0);
	assert(count_task_threads() == baseline_threads);
	assert(futex_get(&budget.active_workers) == 0);

	/* A 1 MiB batch creates workers when at least two CPUs are available. */
	memset(decompressed, 0, src_size);
	assert(decompress_jobs_parallel(
		       &wq, jobs, 2, 2 * job_bytes, 0,
		       record_decompression_overlap, &overlap_calls) == 0);
	assert(memcmp(src, decompressed, 2 * job_bytes) == 0);
	expected_threads = min(available_threads, 2U);
	assert(overlap_calls == (expected_threads > 1));
	assert((wq.nr_workers > 0) == (expected_threads > 1));
	assert(futex_get(&budget.active_workers) == 0);

	memset(decompressed, 0, src_size);
	assert(decompress_jobs_parallel(&wq, jobs, nr_jobs, src_size, 0, NULL, NULL) == 0);
	assert(memcmp(src, decompressed, src_size) == 0);
	assert(futex_get(&budget.active_workers) == 0);

	/* Zero jobs use the same serial fallback and persistent worker pool. */
	memset(decompressed, 0xa5, src_size);
	for (job = 0; job < nr_jobs; job++) {
		jobs[job].src = NULL;
		jobs[job].compressed_size = 0;
	}
	assert(decompress_jobs_parallel(&wq, jobs, 1, job_bytes, 1, NULL, NULL) == 0);
	for (job = 0; job < job_bytes; job++)
		assert(decompressed[job] == 0);

	memset(decompressed, 0xa5, src_size);
	assert(decompress_jobs_parallel(&wq, jobs, nr_jobs, src_size, 0, NULL, NULL) == 0);
	for (job = 0; job < src_size; job++)
		assert(decompressed[job] == 0);

	cr_work_queue_destroy(&wq);
	cr_work_set_shared_budget(NULL);
	wait_for_task_threads(baseline_threads);

	free(jobs);
	free(decompressed);
	free(compressed);
	free(src);
}

static void test_block_roundtrip(const char *src, unsigned int n_pages,
				 int acceleration)
{
	size_t block_bytes = (size_t)n_pages * PAGE_SIZE;
	size_t cap = BLOCK_COMPRESSED_SIZE_BOUND(n_pages);
	char *cbuf = malloc(cap);
	char *dec = malloc(block_bytes);
	int cs;

	assert(cbuf && dec);
	cs = compress_block(src, n_pages, cbuf, cap, acceleration);
	assert(cs >= 0);
	assert((size_t)cs <= block_bytes);
	assert(decompress_block(cbuf, cs, n_pages, dec) == 0);
	assert(memcmp(src, dec, block_bytes) == 0);

	free(cbuf);
	free(dec);
}

static void test_block_compression(void)
{
	unsigned int sizes[] = { 1, DEFAULT_BLOCK_PAGES, MAX_BLOCK_PAGES };
	const int accels[] = { 1, 4, 32 };
	unsigned int s, a;

	for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
		unsigned int n_pages = sizes[s];
		size_t block_bytes = (size_t)n_pages * PAGE_SIZE;
		char *src = malloc(block_bytes);
		size_t cap = BLOCK_COMPRESSED_SIZE_BOUND(n_pages);
		char *cbuf = malloc(cap);
		size_t i;

		assert(src && cbuf);

		for (a = 0; a < sizeof(accels) / sizeof(accels[0]); a++) {
			int accel = accels[a];
			int cs;
			uint32_t state;

			/* All-zero block: must short-circuit to 0 bytes. */
			memset(src, 0, block_bytes);
			cs = compress_block(src, n_pages, cbuf, cap, accel);
			assert(cs == 0);
			test_block_roundtrip(src, n_pages, accel);

			/* Repeating pattern: should compress well. */
			for (i = 0; i < block_bytes; i++)
				src[i] = (char)(i & 0xff);
			cs = compress_block(src, n_pages, cbuf, cap, accel);
			assert(cs > 0);
			assert((size_t)cs < block_bytes);
			test_block_roundtrip(src, n_pages, accel);

			/* Deterministic high-entropy bytes must use the raw fallback. */
			state = 0x9e3779b9U ^ (a + 1) ^ n_pages;

			for (i = 0; i < block_bytes; i++) {
				state ^= state << 13;
				state ^= state >> 17;
				state ^= state << 5;
				src[i] = (char)(state >> 24);
			}
			cs = compress_block(src, n_pages, cbuf, cap, accel);
			assert((size_t)cs == block_bytes);
			test_block_roundtrip(src, n_pages, accel);

			/* Mostly zeros with one non-zero island. */
			memset(src, 0, block_bytes);
			memset(src + (n_pages / 2) * PAGE_SIZE, 0xab, PAGE_SIZE);
			cs = compress_block(src, n_pages, cbuf, cap, accel);
			assert(cs > 0);
			assert((size_t)cs < block_bytes);
			test_block_roundtrip(src, n_pages, accel);
		}

		free(src);
		free(cbuf);
	}
}

#endif

static pid_t test_wq_caller_tid;

struct test_wq_shared {
	struct cr_work_budget budget;
	atomic_t total_completed;
	atomic_t active_in_workers;
	atomic_t max_observed_in_workers;
};

static int test_wq_inc_fn(void *arg)
{
	struct test_wq_shared *sh = arg;
	pid_t tid = syscall(SYS_gettid);
	int cur, max_obs;

	if (tid != test_wq_caller_tid) {
		cur = atomic_inc_return(&sh->active_in_workers);
		max_obs = atomic_read(&sh->max_observed_in_workers);
		while (cur > max_obs) {
			if (atomic_cmpxchg(&sh->max_observed_in_workers, max_obs, cur) == max_obs)
				break;
			max_obs = atomic_read(&sh->max_observed_in_workers);
		}
		usleep(200);
		atomic_dec(&sh->active_in_workers);
	}

	atomic_inc(&sh->total_completed);
	return 0;
}

static int test_wq_fail_fn(void *arg)
{
	int *val = arg;

	return *val;
}

static void test_work_queue(void)
{
	struct test_wq_shared *sh;
	struct cr_work_queue q;
	const int nr_items = CR_WORK_QUEUE_SIZE * 3;
	const int nr_procs = 4;
	int i, err_code = -42;

	sh = mmap(NULL, sizeof(*sh), PROT_READ | PROT_WRITE,
		  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	assert(sh != MAP_FAILED);
	memset(sh, 0, sizeof(*sh));
	test_wq_caller_tid = syscall(SYS_gettid);

	cr_work_budget_init(&sh->budget, 2);
	/* Force max_workers to 2 for cross-process slot cap testing */
	sh->budget.max_workers = 2;

	assert(cr_work_queue_init(&q, &sh->budget) == 0);
	/* Lazy creation: zero workers created before cr_work_submit */
	assert(q.nr_workers == 0);

	for (i = 0; i < nr_items; i++)
		assert(cr_work_submit(&q, test_wq_inc_fn, sh) == 0);

	assert(cr_work_wait(&q) == 0);
	assert(atomic_read(&sh->total_completed) == nr_items);
	assert(q.nr_workers <= 2);

	/* Error propagation check */
	assert(cr_work_submit(&q, test_wq_fail_fn, &err_code) == 0);
	assert(cr_work_wait(&q) == -42);
	/* Subsequent wait after error reset should return 0 */
	assert(cr_work_wait(&q) == 0);

	cr_work_queue_destroy(&q);

	/* Multi-process shared budget concurrency cap test */
	atomic_set(&sh->total_completed, 0);
	atomic_set(&sh->active_in_workers, 0);
	atomic_set(&sh->max_observed_in_workers, 0);
	cr_work_budget_init(&sh->budget, 2);
	sh->budget.max_workers = 2;

	for (i = 0; i < nr_procs; i++) {
		pid_t pid = fork();

		assert(pid >= 0);
		if (pid == 0) {
			struct cr_work_queue child_q;
			int j;

			test_wq_caller_tid = syscall(SYS_gettid);
			assert(cr_work_queue_init(&child_q, &sh->budget) == 0);
			for (j = 0; j < 32; j++)
				assert(cr_work_submit(&child_q, test_wq_inc_fn, sh) == 0);
			assert(cr_work_wait(&child_q) == 0);
			cr_work_queue_destroy(&child_q);
			_exit(0);
		}
	}

	for (i = 0; i < nr_procs; i++) {
		int status = 0;

		assert(wait(&status) > 0);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	}

	assert(atomic_read(&sh->total_completed) == nr_procs * 32);
	assert(atomic_read(&sh->max_observed_in_workers) <= 2);
	munmap(sh, sizeof(*sh));
}

int main(int argc, char *argv[], char *envp[])
{
	char **configuration;
	int i;

	configuration = malloc(10 * sizeof(char *));
	log_init(NULL);

	test_bfd();
	test_bwrite();
	test_pagemap_offset_alignment();
	test_plugin_options();

	i = parse_statement(0, "", configuration);
	assert(i == 0);

	i = parse_statement(0, "\n", configuration);
	assert(i == 0);

	i = parse_statement(0, "# comment\n", configuration);
	assert(i == 0);

	i = parse_statement(0, "#comment\n", configuration);
	assert(i == 0);

	i = parse_statement(0, "tcp-close #comment\n", configuration);
	assert(i == 1);
	assert(!strcmp(configuration[0], "--tcp-close"));

	i = parse_statement(0, " tcp-close #comment\n", configuration);
	assert(i == 1);
	assert(!strcmp(configuration[0], "--tcp-close"));

	i = parse_statement(0, "test \"test\"\n", configuration);
	assert(i == 2);
	assert(!strcmp(configuration[0], "--test"));
	assert(!strcmp(configuration[1], "test"));

	i = parse_statement(0, "dsfa \"aaaaa \\\"bbbbbb\\\"\"\n", configuration);
	assert(i == 2);
	assert(!strcmp(configuration[0], "--dsfa"));
	assert(!strcmp(configuration[1], "aaaaa \"bbbbbb\""));

	i = parse_statement(0, "verbosity 4\n", configuration);
	assert(i == 2);
	assert(!strcmp(configuration[0], "--verbosity"));
	assert(!strcmp(configuration[1], "4"));

	i = parse_statement(0, "verbosity \"\n", configuration);
	assert(i == -1);

	i = parse_statement(0, "verbosity 4#comment\n", configuration);
	assert(i == 2);
	assert(!strcmp(configuration[0], "--verbosity"));
	assert(!strcmp(configuration[1], "4"));

	i = parse_statement(0, "verbosity 4 #comment\n", configuration);
	assert(i == 2);
	assert(!strcmp(configuration[0], "--verbosity"));
	assert(!strcmp(configuration[1], "4"));

	i = parse_statement(0, "verbosity 4  #comment\n", configuration);
	assert(i == 2);
	assert(!strcmp(configuration[0], "--verbosity"));
	assert(!strcmp(configuration[1], "4"));

	i = parse_statement(0, "verbosity 4 no-comment\n", configuration);
	assert(i == -1);

	i = parse_statement(0, "lsm-profile \"\" # more comments\n", configuration);
	assert(i == 2);
	assert(!strcmp(configuration[0], "--lsm-profile"));
	assert(!strcmp(configuration[1], ""));

	i = parse_statement(0, "lsm-profile \"something\"# comment\n", configuration);
	assert(i == 2);
	assert(!strcmp(configuration[0], "--lsm-profile"));
	assert(!strcmp(configuration[1], "something"));

	i = parse_statement(0, "#\n", configuration);
	assert(i == 0);

	i = parse_statement(0, "lsm-profile \"selinux:something\\\"with\\\"quotes\"\n", configuration);
	assert(i == 2);
	assert(!strcmp(configuration[0], "--lsm-profile"));
	assert(!strcmp(configuration[1], "selinux:something\"with\"quotes"));

	i = parse_statement(0, "work-dir \"/tmp with spaces\" no-comment\n", configuration);
	assert(i == -1);

	i = parse_statement(0, "work-dir \"/tmp with spaces\"\n", configuration);
	assert(i == 2);
	assert(!strcmp(configuration[0], "--work-dir"));
	assert(!strcmp(configuration[1], "/tmp with spaces"));

	i = parse_statement(0, "a b c d e f g h i\n", configuration);
	assert(i == -1);

	/* get_relative_path */
	/* different kinds of representation of "/" */
	assert(!strcmp(get_relative_path("/", "/"), ""));
	assert(!strcmp(get_relative_path("/", ""), ""));
	assert(!strcmp(get_relative_path("", "/"), ""));
	assert(!strcmp(get_relative_path(".", "/"), ""));
	assert(!strcmp(get_relative_path("/", "."), ""));
	assert(!strcmp(get_relative_path("/", "./"), ""));
	assert(!strcmp(get_relative_path("./", "/"), ""));
	assert(!strcmp(get_relative_path("/.", "./"), ""));
	assert(!strcmp(get_relative_path("./", "/."), ""));
	assert(!strcmp(get_relative_path(".//////.", ""), ""));
	assert(!strcmp(get_relative_path("/./", ""), ""));

	/* all relative paths given are assumed relative to "/" */
	assert(!strcmp(get_relative_path("/a/b/c", "a/b/c"), ""));

	/* multiple slashes are ignored, only directory names matter */
	assert(!strcmp(get_relative_path("///alfa///beta///gamma///", "//alfa//beta//gamma//"), ""));

	/* returned path is always relative */
	assert(!strcmp(get_relative_path("/a/b/c", "/"), "a/b/c"));
	assert(!strcmp(get_relative_path("/a/b/c", "/a/b"), "c"));

	/* single dots supported */
	assert(!strcmp(get_relative_path("./a/b", "a/"), "b"));

	/* double dots are partially supported */
	assert(!strcmp(get_relative_path("a/../b", "a"), "../b"));
	assert(!strcmp(get_relative_path("a/../b", "a/.."), "b"));
	assert(!get_relative_path("a/../b/c", "b"));

	/* if second path is not subpath - NULL returned */
	assert(!get_relative_path("/a/b/c", "/a/b/d"));
	assert(!get_relative_path("/a/b", "/a/b/c"));
	assert(!get_relative_path("/a/b/c/d", "b/c/d"));

	assert(!strcmp(get_relative_path("./a////.///./b//././c", "///./a/b"), "c"));

	/* leaves punctuation in returned string as is */
	assert(!strcmp(get_relative_path("./a////.///./b//././c", "a"), "b//././c"));

#ifdef CONFIG_LZ4
	test_compression();
	test_encoded_stream_zero_batch();
	test_parallel_decompression();
	test_block_compression();
#endif
	test_work_queue();

	pr_msg("OK\n");
	return 0;
}
