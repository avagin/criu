#ifndef __CR_COMPRESSION_H__
#define __CR_COMPRESSION_H__

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "common/lock.h"
#include "page.h"
#include "work-queue.h"

struct page_read;
struct page_read_iov;

/*
 * Compression mode for memory pages. Stored in opts.compress_mode and
 * encoded in inventory_entry.compress and criu_opts.compress on the wire.
 *
 * Single source of truth: COMPRESS_OFF (=0) means no compression. There
 * is no separate "compression enabled" boolean; sites that want a
 * predicate use `if (opts.compress_mode)`.
 */
enum compress_mode {
	COMPRESS_OFF		= 0,
	COMPRESS_BLOCK		= 1,
};

/* Keep block memory and CLI limits stable across host page sizes. */
#define MAX_BLOCK_SIZE		(4UL * 1024 * 1024)
#define DEFAULT_BLOCK_SIZE	(256UL * 1024)
#define MAX_BLOCK_PAGES		(MAX_BLOCK_SIZE / PAGE_SIZE)
#define DEFAULT_BLOCK_PAGES	(DEFAULT_BLOCK_SIZE / PAGE_SIZE)

/*
 * Per-task and minimum batch limits for parallel memory restore:
 * - Compressed data (CPU-bound LZ4 decoding): 512 KiB per task, 1 MiB min batch.
 * - Uncompressed data (I/O-bound pread/memset): 4 MiB per task, 8 MiB min batch.
 */
#define PARALLEL_COMPRESSED_TASK_BYTES      (512UL << 10)
#define PARALLEL_COMPRESSED_MIN_BATCH_BYTES (1UL << 20)
#define PARALLEL_RESTORE_MIN_BATCH_BYTES    PARALLEL_COMPRESSED_MIN_BATCH_BYTES

#define PARALLEL_UNCOMPRESSED_TASK_BYTES      (4UL << 20)
#define PARALLEL_UNCOMPRESSED_MIN_BATCH_BYTES (8UL << 20)

/* LZ4 worst-case compressed size for one page: src + src/255 + 16 */
#define PAGE_COMPRESSED_SIZE_BOUND (PAGE_SIZE + (PAGE_SIZE / 255) + 16)

/*
 * LZ4 worst-case compressed size for a block of n_pages pages.
 * Same formula as PAGE_COMPRESSED_SIZE_BOUND but with n_pages*PAGE_SIZE
 * as the input size.
 */
#define BLOCK_COMPRESSED_SIZE_BOUND(n_pages) \
	((size_t)(n_pages) * PAGE_SIZE + ((size_t)(n_pages) * PAGE_SIZE / 255) + 16)

/*
 * Compression threshold: store raw if compressed size is at or above this.
 * Pages that only compress by a small amount are not worth the
 * decompression cost on restore.
 */
#define PAGE_COMPRESSION_THRESHOLD (PAGE_SIZE * 7 / 8)
#define BLOCK_COMPRESSION_THRESHOLD(block_bytes) ((block_bytes) * 7 / 8)

/*
 * Default LZ4 acceleration level for LZ4_compress_fast().
 * Acceleration controls how many positions the compressor
 * probes in its hash table when searching for matches.
 * Value 1 performs the most thorough search and gives the best ratio.
 * Higher values skip more match candidates, resulting in
 * faster compression but fewer and shorter matches.
 * The acceleration setting does not affect decompression.
 * Valid range: 1 to LZ4_MAX_ACCELERATION.
 */
#define LZ4_MAX_ACCELERATION	65537
#define LZ4_DEFAULT_ACCELERATION 1

/*
 * Detect zero-filled pages. Use memcpy() for word loads so callers can
 * pass stack buffers without requiring unsigned-long alignment.
 */
static inline bool page_is_all_zero(const char *page)
{
	unsigned int last = PAGE_SIZE / sizeof(unsigned long) - 1;
	unsigned long word;
	unsigned int i;

	/*
	 * Check last word first: pages are often zero at the start
	 * but have non-zero data near the end (e.g. stack, heap).
	 * See kernel commit 0ca0c24e3211 ("mm: zswap: check the last
	 * page in a folio first").
	 */
	__builtin_memcpy(&word, page + (size_t)last * sizeof(word), sizeof(word));
	if (word)
		return false;

	for (i = 0; i < last; i++) {
		__builtin_memcpy(&word, page + (size_t)i * sizeof(word), sizeof(word));
		if (word)
			return false;
	}
	return true;
}

/*
 * One block that can be restored independently. Jobs passed together must
 * write to disjoint destinations. A zero compressed size clears the block;
 * other jobs hold LZ4 payloads. The caller handles raw blocks instead of
 * submitting them as jobs.
 */
struct decompress_job {
	const char *src;
	char *dst;
	uint32_t compressed_size;
	uint16_t pages;
	int block_index;
};

typedef void (*decompression_caller_work_fn)(void *arg);

struct encoded_prefetch {
	char *buffer;
	size_t count;
	size_t done;
	off_t offset;
	int fd;
	int saved_errno;
	bool complete;
};

/*
 * Reusable storage for encoded reads. The top-level page reader owns one
 * context for its whole parent chain. Buffers are reused during one active
 * read and then released; the work queue remains reusable across reads.
 */
struct encoded_read_ctx {
	struct decompress_job *jobs;
	size_t jobs_cap;
	char *compressed;
	size_t compressed_cap;
	char *prefetch_buffer;
	size_t prefetch_cap;
	const void *prefetched_token;
	struct encoded_prefetch prefetch;
	char *scratch;
	size_t scratch_cap;
	struct cr_work_queue wq;
	bool wq_initialized;
	bool batch_acquired;
	bool prefetch_batch_acquired;
};

static inline bool compressed_restore_has_parallel_capacity(
	unsigned int requested_threads)
{
	return cr_work_has_parallel_capacity(requested_threads);
}

#ifdef CONFIG_LZ4

/*
 * Compress @n_pages pages from @src into one LZ4 compressed block.
 *
 * Returns the size to store in compressed_size[]:
 * - 0: all-zero block, no payload
 * - n_pages * PAGE_SIZE: raw payload in @dst
 * - otherwise: LZ4 payload in @dst
 *
 * Returns -1 on error.
 */
int compress_block(const char *src, unsigned int n_pages, char *dst,
		   size_t dst_cap, int acceleration);

/*
 * Inverse of compress_block(). @compressed_size is the value the
 * caller stored at compression time. Always writes n_pages*PAGE_SIZE
 * bytes into @dst.
 */
int decompress_block(const char *src, int compressed_size,
		     unsigned int n_pages, char *dst);

int decompress_jobs_parallel(struct cr_work_queue *wq,
			     struct decompress_job *jobs,
			     size_t nr_jobs,
			     size_t total_uncompressed,
			     unsigned int requested_threads,
			     decompression_caller_work_fn caller_work,
			     void *caller_work_arg);

void encoded_read_ctx_begin_work(struct encoded_read_ctx *ctx);
void encoded_read_ctx_end_work(struct encoded_read_ctx *ctx);
void encoded_read_ctx_fini(struct encoded_read_ctx *ctx);
void encoded_prefetch_read(void *arg);
void encoded_prefetch_disable(struct encoded_read_ctx *ctx);
bool encoded_prefetch_prepare(struct encoded_read_ctx *ctx, int fd,
			      off_t offset, size_t count);
void encoded_prefetch_publish(struct encoded_read_ctx *ctx,
			      const void *token);
int encoded_prefetch_take(struct encoded_read_ctx *ctx,
			  const void *token, size_t expected_count);

struct encoded_read_ctx *encoded_read_ctx_alloc(void);
void encoded_read_ctx_destroy(struct encoded_read_ctx *ctx);
int validate_direct_compressed_iov(const struct page_read_iov *piov);
int encoded_stream_read_batch(int fd, void *buf, const uint32_t *block_sizes,
			      unsigned long batch_pages, size_t batch_payload,
			      size_t nr_jobs, struct encoded_read_ctx *ctx,
			      unsigned long first_page_idx);
int encoded_async_read_batch(int fd, struct page_read_iov *piov,
			     struct page_read_iov *next, bool is_last,
			     struct encoded_read_ctx *ctx,
			     const struct page_read *pr);

#else /* !CONFIG_LZ4 */

static inline int compress_block(const char *src, unsigned int n_pages,
				 char *dst, size_t dst_cap, int accel)
{
	return -1;
}

static inline int decompress_block(const char *src, int comp_sz,
				   unsigned int n_pages, char *dst)
{
	return -1;
}

static inline int decompress_jobs_parallel(
	struct cr_work_queue *wq, struct decompress_job *jobs,
	size_t nr_jobs, size_t total_uncompressed,
	unsigned int requested_threads,
	decompression_caller_work_fn caller_work, void *caller_work_arg)
{
	return -1;
}

static inline void encoded_read_ctx_begin_work(struct encoded_read_ctx *ctx)
{
}

static inline void encoded_read_ctx_end_work(struct encoded_read_ctx *ctx)
{
}

static inline void encoded_read_ctx_fini(struct encoded_read_ctx *ctx)
{
}

static inline void encoded_prefetch_read(void *arg)
{
}

static inline void encoded_prefetch_disable(struct encoded_read_ctx *ctx)
{
}

static inline bool encoded_prefetch_prepare(struct encoded_read_ctx *ctx,
					    int fd, off_t offset, size_t count)
{
	return false;
}

static inline void encoded_prefetch_publish(struct encoded_read_ctx *ctx,
					    const void *token)
{
}

static inline int encoded_prefetch_take(struct encoded_read_ctx *ctx,
					const void *token, size_t expected_count)
{
	return 0;
}

static inline struct encoded_read_ctx *encoded_read_ctx_alloc(void)
{
	return NULL;
}

static inline void encoded_read_ctx_destroy(struct encoded_read_ctx *ctx)
{
}

static inline int validate_direct_compressed_iov(const struct page_read_iov *piov)
{
	return -1;
}

static inline int encoded_stream_read_batch(int fd, void *buf, const uint32_t *block_sizes,
					    unsigned long batch_pages, size_t batch_payload,
					    size_t nr_jobs, struct encoded_read_ctx *ctx,
					    unsigned long first_page_idx)
{
	return -1;
}

static inline int encoded_async_read_batch(int fd, struct page_read_iov *piov,
					   struct page_read_iov *next, bool is_last,
					   struct encoded_read_ctx *ctx,
					   const struct page_read *pr)
{
	return -1;
}

#endif /* CONFIG_LZ4 */

#endif /* __CR_COMPRESSION_H__ */
