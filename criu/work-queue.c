#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "common/bug.h"
#include "common/compiler.h"
#include "common/lock.h"
#include "common/xmalloc.h"
#include "util.h"
#include "work-queue.h"

#undef LOG_PREFIX
#define LOG_PREFIX "work-queue: "

#define CR_WORK_ACTIVE_MASK	0xffffU
#define CR_WORK_GEN_INC		0x10000U
#define CR_WORK_MAX_WORKERS	0x7fffU
#define CR_WORK_STACK_SIZE	(256UL << 10)

static struct cr_work_budget *shared_work_budget;
static struct cr_work_budget default_work_budget;
static struct cr_work_queue task_work_queue;

static unsigned int cr_work_available_cpus(void)
{
	cpu_set_t *affinity;
	size_t affinity_size;
	size_t nr_cpus = CPU_SETSIZE;
	long available_cpus;
	int affinity_errno;

	available_cpus = sysconf(_SC_NPROCESSORS_CONF);
	if (available_cpus > (long)nr_cpus)
		nr_cpus = (size_t)available_cpus;

	for (;;) {
		affinity = CPU_ALLOC(nr_cpus);
		if (!affinity)
			return 1;
		affinity_size = CPU_ALLOC_SIZE(nr_cpus);
		CPU_ZERO_S(affinity_size, affinity);
		if (sched_getaffinity(0, affinity_size, affinity) == 0) {
			available_cpus = CPU_COUNT_S(affinity_size, affinity);
			CPU_FREE(affinity);
			break;
		}
		affinity_errno = errno;
		CPU_FREE(affinity);
		if (affinity_errno != EINVAL || nr_cpus > UINT_MAX / 2)
			return 1;
		nr_cpus *= 2;
	}

	if (available_cpus < 1)
		return 1;
	return (unsigned int)available_cpus;
}

unsigned int cr_work_thread_limit(unsigned int requested, unsigned int available_cpus)
{
	if (!available_cpus)
		available_cpus = 1;
	if (!requested)
		return available_cpus;
	return min(requested, available_cpus);
}

void cr_work_budget_init(struct cr_work_budget *budget, unsigned int requested_threads)
{
	unsigned int avail = cr_work_available_cpus();
	unsigned int threads;
	unsigned int max_workers;

	if (requested_threads < 2)
		threads = cr_work_thread_limit(0, avail);
	else
		threads = cr_work_thread_limit(requested_threads, avail);

	if (requested_threads > threads)
		pr_warn("Reducing worker concurrency from %u to %u (available CPUs)\n",
			requested_threads, threads);

	max_workers = threads > 1 ? threads - 1 : 0;
	if (max_workers > CR_WORK_MAX_WORKERS)
		max_workers = CR_WORK_MAX_WORKERS;

	futex_set(&budget->active_workers, 0);
	futex_set(&budget->batches, CR_WORK_MAX_BATCHES);
	budget->max_workers = max_workers;
	budget->thread_capacity = threads;
}

void cr_work_set_shared_budget(struct cr_work_budget *budget)
{
	shared_work_budget = budget;
}

static struct cr_work_budget *cr_work_get_budget(struct cr_work_budget *budget)
{
	if (budget)
		return budget;
	if (shared_work_budget)
		return shared_work_budget;

	if (!default_work_budget.thread_capacity)
		cr_work_budget_init(&default_work_budget, 0);

	return &default_work_budget;
}

bool cr_work_has_parallel_capacity(unsigned int requested_threads)
{
	struct cr_work_budget *budget = cr_work_get_budget(NULL);

	if (requested_threads == 1 || !budget)
		return false;

	return cr_work_thread_limit(requested_threads, budget->thread_capacity) > 1 &&
	       budget->max_workers > 0;
}

void cr_work_batch_acquire(void)
{
	struct cr_work_budget *budget = shared_work_budget;

	if (!budget)
		return;

	for (;;) {
		unsigned int current = futex_get(&budget->batches);

		if (!current) {
			futex_wait_while_eq(&budget->batches, 0);
			continue;
		}
		if ((unsigned int)atomic_cmpxchg(&budget->batches.raw, current, current - 1) == current)
			return;
	}
}

bool cr_work_batch_try_acquire(void)
{
	struct cr_work_budget *budget = shared_work_budget;
	unsigned int current;

	if (!budget)
		return true;

	current = futex_get(&budget->batches);
	while (current) {
		unsigned int previous;

		previous = atomic_cmpxchg(&budget->batches.raw, current, current - 1);
		if (previous == current)
			return true;
		current = previous;
	}

	return false;
}

void cr_work_batch_release(void)
{
	struct cr_work_budget *budget = shared_work_budget;

	if (!budget)
		return;

	atomic_inc(&budget->batches.raw);
	futex_wake(&budget->batches);
}

static void cr_work_wake_waiters(struct cr_work_budget *budget)
{
	atomic_add(CR_WORK_GEN_INC, &budget->active_workers.raw);
	futex_wake(&budget->active_workers);
}

static bool cr_work_acquire_slot(struct cr_work_queue *q)
{
	struct cr_work_budget *budget = q->budget;

	while (1) {
		uint32_t cur = futex_get(&budget->active_workers);
		uint32_t active = cur & CR_WORK_ACTIVE_MASK;
		int ret;

		if (__atomic_load_n(&q->stop, __ATOMIC_SEQ_CST) ||
		    __atomic_load_n(&q->start, __ATOMIC_SEQ_CST) ==
		    __atomic_load_n(&q->end, __ATOMIC_SEQ_CST))
			return false;

		if (active >= budget->max_workers) {
			ret = sys_futex((uint32_t *)&budget->active_workers.raw.counter,
					FUTEX_WAIT, cur, NULL, NULL, 0);
			LOCK_BUG_ON(ret < 0 && ret != -EWOULDBLOCK && ret != -EINTR);
			continue;
		}

		if ((uint32_t)atomic_cmpxchg(&budget->active_workers.raw, cur, cur + 1) == cur)
			return true;
	}
}

static void cr_work_release_slot(struct cr_work_budget *budget)
{
	atomic_dec(&budget->active_workers.raw);
	futex_wake(&budget->active_workers);
}

static bool cr_work_pop_locked(struct cr_work_queue *q, struct cr_work *item)
{
	unsigned int next_start;

	if (q->start == q->end)
		return false;

	*item = q->buf[q->start & (CR_WORK_QUEUE_SIZE - 1)];
	next_start = q->start + 1;
	__atomic_store_n(&q->start, next_start, __ATOMIC_SEQ_CST);
	pthread_cond_signal(&q->not_full);

	if (next_start == q->end)
		cr_work_wake_waiters(q->budget);
	else if (q->idle_workers > 0)
		pthread_cond_signal(&q->not_empty);

	return true;
}

static void cr_work_complete_locked(struct cr_work_queue *q, int ret)
{
	if (ret && !q->error)
		q->error = ret;

	BUG_ON(!q->inflight);
	q->inflight--;
	if (!q->inflight)
		pthread_cond_broadcast(&q->empty_done);
}

static void *cr_worker_fn(void *arg)
{
	struct cr_work_queue *q = arg;

	while (1) {
		struct cr_work item;

		pthread_mutex_lock(&q->lock);
		q->idle_workers++;
		while (q->start == q->end && !q->stop)
			pthread_cond_wait(&q->not_empty, &q->lock);
		q->idle_workers--;

		if (q->stop && q->start == q->end) {
			pthread_mutex_unlock(&q->lock);
			break;
		}
		pthread_mutex_unlock(&q->lock);

		if (!cr_work_acquire_slot(q))
			continue;

		pthread_mutex_lock(&q->lock);
		while (cr_work_pop_locked(q, &item)) {
			int ret;

			pthread_mutex_unlock(&q->lock);
			ret = item.work_func(item.work_args);
			pthread_mutex_lock(&q->lock);
			cr_work_complete_locked(q, ret);
		}
		pthread_mutex_unlock(&q->lock);

		cr_work_release_slot(q->budget);
	}

	return NULL;
}

static void cr_work_worker_signal_mask(sigset_t *set)
{
	sigfillset(set);
	sigdelset(set, SIGABRT);
	sigdelset(set, SIGBUS);
	sigdelset(set, SIGFPE);
	sigdelset(set, SIGILL);
	sigdelset(set, SIGSEGV);
	sigdelset(set, SIGSYS);
	sigdelset(set, SIGTRAP);
}

static void cr_work_spawn_worker_locked(struct cr_work_queue *q)
{
	pthread_attr_t attr;
	sigset_t mask, oldmask;
	size_t stack_size = CR_WORK_STACK_SIZE;
	int err;

	if (q->nr_workers >= q->budget->max_workers)
		return;

	err = pthread_attr_init(&attr);
	if (err) {
		pr_warn("Unable to init pthread attr: %s\n", strerror(err));
		return;
	}
	if (stack_size < PTHREAD_STACK_MIN)
		stack_size = PTHREAD_STACK_MIN;
	pthread_attr_setstacksize(&attr, stack_size);

	cr_work_worker_signal_mask(&mask);
	if (pthread_sigmask(SIG_SETMASK, &mask, &oldmask) == 0) {
		err = pthread_create(&q->workers[q->nr_workers], &attr, cr_worker_fn, q);
		pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
		if (!err)
			q->nr_workers++;
		else
			pr_warn("Unable to create worker thread: %s\n", strerror(err));
	}

	pthread_attr_destroy(&attr);
}

int cr_work_queue_init(struct cr_work_queue *q, struct cr_work_budget *budget)
{
	memset(q, 0, sizeof(*q));
	q->budget = cr_work_get_budget(budget);

	if (q->budget->max_workers > 0) {
		q->workers = xmalloc(sizeof(pthread_t) * q->budget->max_workers);
		if (!q->workers)
			return -1;
	}

	if (pthread_mutex_init(&q->lock, NULL))
		goto err_free;
	if (pthread_cond_init(&q->not_empty, NULL))
		goto err_mutex;
	if (pthread_cond_init(&q->not_full, NULL))
		goto err_not_empty;
	if (pthread_cond_init(&q->empty_done, NULL))
		goto err_not_full;

	return 0;

err_not_full:
	pthread_cond_destroy(&q->not_full);
err_not_empty:
	pthread_cond_destroy(&q->not_empty);
err_mutex:
	pthread_mutex_destroy(&q->lock);
err_free:
	xfree(q->workers);
	q->workers = NULL;
	return -1;
}

int cr_work_submit(struct cr_work_queue *q, work_func_t func, void *args)
{
	unsigned int next_end;

	if (!func)
		return -1;

	pthread_mutex_lock(&q->lock);
	while ((q->end - q->start) == CR_WORK_QUEUE_SIZE) {
		struct cr_work item;
		int err;

		if (cr_work_pop_locked(q, &item)) {
			pthread_mutex_unlock(&q->lock);
			err = item.work_func(item.work_args);
			pthread_mutex_lock(&q->lock);
			cr_work_complete_locked(q, err);
			continue;
		}
		pthread_cond_wait(&q->not_full, &q->lock);
	}

	q->buf[q->end & (CR_WORK_QUEUE_SIZE - 1)].work_func = func;
	q->buf[q->end & (CR_WORK_QUEUE_SIZE - 1)].work_args = args;
	next_end = q->end + 1;
	__atomic_store_n(&q->end, next_end, __ATOMIC_SEQ_CST);
	q->inflight++;

	if ((q->end - q->start) > 1) {
		if (q->idle_workers > 0)
			pthread_cond_signal(&q->not_empty);
		else if (q->nr_workers < q->budget->max_workers)
			cr_work_spawn_worker_locked(q);
	}

	pthread_mutex_unlock(&q->lock);
	return 0;
}

int cr_work_wait(struct cr_work_queue *q)
{
	struct cr_work item;
	int ret;

	pthread_mutex_lock(&q->lock);
	while (q->inflight > 0) {
		if (cr_work_pop_locked(q, &item)) {
			int err;

			pthread_mutex_unlock(&q->lock);
			err = item.work_func(item.work_args);
			pthread_mutex_lock(&q->lock);
			cr_work_complete_locked(q, err);
			continue;
		}
		pthread_cond_wait(&q->empty_done, &q->lock);
	}

	ret = q->error;
	q->error = 0;
	pthread_mutex_unlock(&q->lock);

	return ret;
}

void cr_work_queue_destroy(struct cr_work_queue *q)
{
	unsigned int i;

	if (!q->budget)
		return;

	cr_work_wait(q);

	pthread_mutex_lock(&q->lock);
	__atomic_store_n(&q->stop, true, __ATOMIC_SEQ_CST);
	pthread_cond_broadcast(&q->not_empty);
	cr_work_wake_waiters(q->budget);
	pthread_mutex_unlock(&q->lock);

	for (i = 0; i < q->nr_workers; i++)
		pthread_join(q->workers[i], NULL);

	pthread_cond_destroy(&q->empty_done);
	pthread_cond_destroy(&q->not_full);
	pthread_cond_destroy(&q->not_empty);
	pthread_mutex_destroy(&q->lock);

	xfree(q->workers);
	memset(q, 0, sizeof(*q));
}

struct cr_work_queue *cr_task_work_queue(void)
{
	if (!task_work_queue.budget) {
		if (cr_work_queue_init(&task_work_queue, NULL))
			return NULL;
	}
	return &task_work_queue;
}

void cr_task_work_queue_destroy(void)
{
	cr_work_queue_destroy(&task_work_queue);
}
