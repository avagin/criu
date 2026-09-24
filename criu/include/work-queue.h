#ifndef __CR_WORK_QUEUE_H__
#define __CR_WORK_QUEUE_H__

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "common/lock.h"

#define CR_WORK_QUEUE_SIZE 256
#define CR_WORK_MAX_BATCHES 2

typedef int (*work_func_t)(void *arg);

struct cr_work {
	work_func_t work_func;
	void *work_args;
};

/*
 * Global concurrency counter placed in shared memory on restore so all
 * CRIU processes (restore tasks, asyncd, etc.) share a single limit on
 * active worker threads and active batch working sets.
 *
 * The lower 16 bits of active_workers hold the current number of active
 * worker threads (0 .. max_workers). The upper 16 bits hold a wakeup
 * generation counter used to wake workers whose local queue drained or
 * stopped while they were waiting for a global slot.
 */
struct cr_work_budget {
	futex_t active_workers;
	futex_t batches;
	unsigned int max_workers;
	unsigned int thread_capacity;
};

struct cr_work_queue {
	struct cr_work buf[CR_WORK_QUEUE_SIZE];
	unsigned int start;
	unsigned int end;

	pthread_mutex_t lock;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
	pthread_cond_t empty_done;

	unsigned int inflight;
	unsigned int idle_workers;
	int error;
	bool stop;

	pthread_t *workers;
	unsigned int nr_workers;
	struct cr_work_budget *budget;
};

extern unsigned int cr_work_thread_limit(unsigned int requested, unsigned int available_cpus);
extern void cr_work_budget_init(struct cr_work_budget *budget, unsigned int requested_threads);
extern void cr_work_set_shared_budget(struct cr_work_budget *budget);
extern bool cr_work_has_parallel_capacity(unsigned int requested_threads);

extern void cr_work_batch_acquire(void);
extern bool cr_work_batch_try_acquire(void);
extern void cr_work_batch_release(void);

extern int cr_work_queue_init(struct cr_work_queue *q, struct cr_work_budget *budget);
extern void cr_work_queue_destroy(struct cr_work_queue *q);

extern int cr_work_submit(struct cr_work_queue *q, work_func_t func, void *args);
extern int cr_work_wait(struct cr_work_queue *q);

#endif /* __CR_WORK_QUEUE_H__ */
