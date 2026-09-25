#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <lz4.h>

#include "page.h"
#include "pagemap.h"
#include "cr_options.h"
#include "util.h"
#include "log.h"
#include "common/bug.h"
#include "compression.h"
#include "common/xmalloc.h"
#include "work-queue.h"

#undef LOG_PREFIX
#define LOG_PREFIX "compression: "

static int decompress_data_nolog(const char *compressed_data,
				 int compressed_size, int original_size,
				 char *decompressed_data)
{
	int ret;

	ret = LZ4_decompress_safe(compressed_data, decompressed_data,
				  compressed_size, original_size);
	return ret == original_size ? 0 : -1;
}

int compress_block(const char *src, unsigned int n_pages, char *dst,
		   size_t dst_cap, int acceleration)
{
	size_t block_bytes;
	unsigned int i;
	int ret;

	if (n_pages == 0 || n_pages > MAX_BLOCK_PAGES) {
		pr_err("compress_block: invalid n_pages %u\n", n_pages);
		return -1;
	}
	if (!src || !dst) {
		pr_err("compress_block: invalid buffer\n");
		return -1;
	}
	block_bytes = (size_t)n_pages * PAGE_SIZE;

	/* Cheap pre-pass: every page in the block zero-filled? */
	for (i = 0; i < n_pages; i++) {
		if (!page_is_all_zero(src + (size_t)i * PAGE_SIZE))
			break;
	}
	if (i == n_pages)
		return 0;

	if (dst_cap < block_bytes) {
		pr_err("compress_block: dst buffer (%zu) smaller than block (%zu)\n",
		       dst_cap, block_bytes);
		return -1;
	}

	if (acceleration < 1)
		acceleration = 1;

	ret = LZ4_compress_fast(src, dst, block_bytes, dst_cap, acceleration);
	if (ret <= 0 || (size_t)ret >= BLOCK_COMPRESSION_THRESHOLD(block_bytes)) {
		/*
		 * LZ4 can fail when dst_cap is below LZ4 worst-case bound
		 * (which the caller should size correctly), or when the
		 * compressed size hits the threshold and we'd rather store
		 * raw. Either way, fall back to raw.
		 */
		memcpy(dst, src, block_bytes);
		return block_bytes;
	}

	return ret;
}

int decompress_block(const char *src, int compressed_size,
		     unsigned int n_pages, char *dst)
{
	size_t block_bytes;

	if (n_pages == 0 || n_pages > MAX_BLOCK_PAGES) {
		pr_err("decompress_block: invalid n_pages %u\n", n_pages);
		return -1;
	}
	if (compressed_size < 0) {
		pr_err("decompress_block: negative compressed_size %d\n",
		       compressed_size);
		return -1;
	}
	if (!dst || (compressed_size && !src)) {
		pr_err("decompress_block: invalid buffer\n");
		return -1;
	}
	block_bytes = (size_t)n_pages * PAGE_SIZE;

	if ((size_t)compressed_size > block_bytes) {
		pr_err("decompress_block: compressed_size %d > block %zu\n",
		       compressed_size, block_bytes);
		return -1;
	}

	if (compressed_size == 0) {
		memset(dst, 0, block_bytes);
		return 0;
	}

	if ((size_t)compressed_size == block_bytes) {
		memcpy(dst, src, block_bytes);
		return 0;
	}

	if (decompress_data_nolog(src, compressed_size, block_bytes, dst)) {
		pr_err("Block decompression failed (compressed_size=%d, n_pages=%u)\n",
		       compressed_size, n_pages);
		return -1;
	}

	return 0;
}

static int decompress_job_run(const struct decompress_job *job)
{
	size_t block_bytes;

	if (!job->pages || job->pages > MAX_BLOCK_PAGES)
		return -1;

	block_bytes = (size_t)job->pages * PAGE_SIZE;
	if (!job->compressed_size) {
		memset(job->dst, 0, block_bytes);
		return 0;
	}
	/* Raw blocks are handled inline by the pagemap reader. */
	if (job->compressed_size >= block_bytes)
		return -1;

	return decompress_data_nolog(job->src, job->compressed_size, block_bytes, job->dst);
}

struct decompress_work_task {
	const struct decompress_job *jobs;
	size_t nr_jobs;
	int failed_block;
};

static int decompress_work_task_fn(void *arg)
{
	struct decompress_work_task *task = arg;
	size_t i;

	task->failed_block = INT_MAX;
	for (i = 0; i < task->nr_jobs; i++) {
		if (decompress_job_run(&task->jobs[i])) {
			task->failed_block = task->jobs[i].block_index;
			return -1;
		}
	}
	return 0;
}

int decompress_jobs_parallel(struct cr_work_queue *wq,
			     struct decompress_job *jobs,
			     size_t nr_jobs,
			     size_t total_uncompressed,
			     unsigned int requested_threads,
			     decompression_caller_work_fn caller_work,
			     void *caller_work_arg)
{
	struct decompress_work_task single_task;
	struct decompress_work_task *tasks = &single_task;
	size_t max_tasks = 1, nr_tasks = 0, start = 0;
	size_t i;
	int ret;

	if (!nr_jobs)
		return 0;
	if (!jobs || !wq)
		return -1;

	if (nr_jobs >= 2 &&
	    total_uncompressed >= PARALLEL_COMPRESSED_MIN_BATCH_BYTES &&
	    compressed_restore_has_parallel_capacity(requested_threads)) {
		max_tasks = min(nr_jobs,
				DIV_ROUND_UP(total_uncompressed, PARALLEL_COMPRESSED_TASK_BYTES));
		if (max_tasks < 2)
			max_tasks = 2;
		tasks = xmalloc(max_tasks * sizeof(*tasks));
		if (!tasks) {
			tasks = &single_task;
			max_tasks = 1;
		}
	}

	while (start < nr_jobs) {
		size_t chunk_bytes = 0;
		size_t end = start;

		if (nr_tasks + 1 == max_tasks) {
			end = nr_jobs;
		} else {
			while (end < nr_jobs &&
			       chunk_bytes < PARALLEL_COMPRESSED_TASK_BYTES &&
			       (nr_jobs - end) > (max_tasks - nr_tasks - 1)) {
				chunk_bytes += (size_t)jobs[end].pages * PAGE_SIZE;
				end++;
			}
			if (end == start)
				end++;
		}

		tasks[nr_tasks].jobs = &jobs[start];
		tasks[nr_tasks].nr_jobs = end - start;
		tasks[nr_tasks].failed_block = INT_MAX;
		nr_tasks++;
		start = end;
	}

	for (i = 0; i < nr_tasks; i++) {
		if (cr_work_submit(wq, decompress_work_task_fn, &tasks[i])) {
			cr_work_wait(wq);
			if (tasks != &single_task)
				xfree(tasks);
			return -1;
		}
	}

	if (caller_work && nr_tasks > 1)
		caller_work(caller_work_arg);

	ret = cr_work_wait(wq);
	if (ret) {
		int earliest = INT_MAX;

		for (i = 0; i < nr_tasks; i++) {
			if (tasks[i].failed_block < earliest)
				earliest = tasks[i].failed_block;
		}
		pr_err("Decompression failed at block %d\n", earliest);
		if (tasks != &single_task)
			xfree(tasks);
		return -1;
	}

	if (tasks != &single_task)
		xfree(tasks);
	return 0;
}

void encoded_read_ctx_begin_work(struct encoded_read_ctx *ctx)
{
	BUG_ON(ctx->batch_acquired);
	cr_work_batch_acquire();
	ctx->batch_acquired = true;
}

void encoded_read_ctx_end_work(struct encoded_read_ctx *ctx)
{
	if (!ctx || !ctx->batch_acquired)
		return;

	xfree(ctx->scratch);
	ctx->scratch = NULL;
	ctx->scratch_cap = 0;
	xfree(ctx->compressed);
	ctx->compressed = NULL;
	ctx->compressed_cap = 0;
	xfree(ctx->prefetch_buffer);
	ctx->prefetch_buffer = NULL;
	ctx->prefetch_cap = 0;
	ctx->prefetched_token = NULL;
	memset(&ctx->prefetch, 0, sizeof(ctx->prefetch));
	xfree(ctx->jobs);
	ctx->jobs = NULL;
	ctx->jobs_cap = 0;
	if (ctx->prefetch_batch_acquired) {
		ctx->prefetch_batch_acquired = false;
		cr_work_batch_release();
	}
	ctx->batch_acquired = false;
	cr_work_batch_release();
}

void encoded_read_ctx_fini(struct encoded_read_ctx *ctx)
{
	if (!ctx)
		return;
	encoded_read_ctx_end_work(ctx);
}

void encoded_prefetch_read(void *arg)
{
	struct encoded_prefetch *prefetch = arg;

	while (prefetch->done < prefetch->count) {
		ssize_t ret;

		ret = pread(prefetch->fd, prefetch->buffer + prefetch->done,
			    prefetch->count - prefetch->done,
			    prefetch->offset + (off_t)prefetch->done);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			prefetch->saved_errno = errno;
			break;
		}
		if (!ret)
			break;
		prefetch->done += ret;
	}
	prefetch->complete = true;
}

void encoded_prefetch_disable(struct encoded_read_ctx *ctx)
{
	ctx->prefetched_token = NULL;
	memset(&ctx->prefetch, 0, sizeof(ctx->prefetch));
	xfree(ctx->prefetch_buffer);
	ctx->prefetch_buffer = NULL;
	ctx->prefetch_cap = 0;
	if (ctx->prefetch_batch_acquired) {
		ctx->prefetch_batch_acquired = false;
		cr_work_batch_release();
	}
}

bool encoded_prefetch_prepare(struct encoded_read_ctx *ctx, int fd,
			      off_t offset, size_t count)
{
	void *new_buffer;

	BUG_ON(ctx->prefetched_token || ctx->prefetch.complete || !count);
	if (!ctx->prefetch_batch_acquired) {
		if (!cr_work_batch_try_acquire())
			return false;
		ctx->prefetch_batch_acquired = true;
	}
	if (ctx->prefetch_cap < count) {
		new_buffer = xrealloc(ctx->prefetch_buffer, count);
		if (!new_buffer) {
			encoded_prefetch_disable(ctx);
			return false;
		}
		ctx->prefetch_buffer = new_buffer;
		ctx->prefetch_cap = count;
	}

	ctx->prefetch.buffer = ctx->prefetch_buffer;
	ctx->prefetch.count = count;
	ctx->prefetch.done = 0;
	ctx->prefetch.offset = offset;
	ctx->prefetch.fd = fd;
	ctx->prefetch.saved_errno = 0;
	return true;
}

void encoded_prefetch_publish(struct encoded_read_ctx *ctx,
			      const void *token)
{
	void *buffer;
	size_t capacity;

	BUG_ON(!ctx->prefetch.complete);
	buffer = ctx->compressed;
	capacity = ctx->compressed_cap;
	ctx->compressed = ctx->prefetch_buffer;
	ctx->compressed_cap = ctx->prefetch_cap;
	ctx->prefetch_buffer = buffer;
	ctx->prefetch_cap = capacity;
	ctx->prefetched_token = token;
}

int encoded_prefetch_take(struct encoded_read_ctx *ctx,
			  const void *token, size_t expected_count)
{
	if (!ctx->prefetched_token)
		return 0;
	if (ctx->prefetched_token != token) {
		pr_err("Encoded prefetch does not match the next request\n");
		return -1;
	}
	ctx->prefetched_token = NULL;
	ctx->prefetch.complete = false;
	if (ctx->prefetch.count != expected_count) {
		pr_err("Encoded prefetch size changed before use\n");
		return -1;
	}

	if (ctx->prefetch.saved_errno) {
		errno = ctx->prefetch.saved_errno;
		pr_perror("Can't read %zu encoded bytes at offset %lld",
			  ctx->prefetch.count, (long long)ctx->prefetch.offset);
		return -1;
	}
	if (ctx->prefetch.done != ctx->prefetch.count) {
		pr_err("Short encoded read %zu/%zu at offset %lld\n",
		       ctx->prefetch.done, ctx->prefetch.count,
		       (long long)ctx->prefetch.offset);
		return -1;
	}

	return 1;
}

struct encoded_read_ctx *encoded_read_ctx_alloc(void)
{
	return xzalloc(sizeof(struct encoded_read_ctx));
}

void encoded_read_ctx_destroy(struct encoded_read_ctx *ctx)
{
	if (ctx) {
		encoded_read_ctx_fini(ctx);
		xfree(ctx);
	}
}

int validate_direct_compressed_iov(const struct page_read_iov *piov)
{
	uint64_t payload_bytes = 0;
	uint64_t output_bytes = 0;
	uint64_t iov_bytes = 0;
	size_t i;

	if (piov->storage != VMA_IO_PACKED_RAW &&
	    piov->storage != VMA_IO_ZERO)
		return -1;
	if (!piov->b_layout.nr_blocks || !piov->b_layout.sizes) {
		pr_err("Direct compressed I/O job has no block metadata\n");
		return -1;
	}
	if (piov->b_layout.pages_per_block && !piov->block_pages) {
		pr_err("Direct block I/O job has no block-page metadata\n");
		return -1;
	}

	for (i = 0; i < piov->b_layout.nr_blocks; i++) {
		unsigned int block_pages = piov->b_layout.pages_per_block ?
						piov->block_pages[i] : 1;
		uint64_t block_bytes;
		uint32_t compressed_size = piov->b_layout.sizes[i];

		if (!block_pages ||
		    (piov->b_layout.pages_per_block && block_pages > piov->b_layout.pages_per_block)) {
			pr_err("Invalid page count %u for direct block %zu\n",
			       block_pages, i);
			return -1;
		}
		block_bytes = (uint64_t)block_pages * PAGE_SIZE;
		if (piov->storage == VMA_IO_PACKED_RAW &&
		    compressed_size != block_bytes) {
			pr_err("Packed-raw block %zu has size %u, expected %" PRIu64 "\n",
			       i, compressed_size, block_bytes);
			return -1;
		}
		if (piov->storage == VMA_IO_ZERO && compressed_size) {
			pr_err("Zero block %zu has payload size %u\n", i,
			       compressed_size);
			return -1;
		}
		if (payload_bytes > UINT64_MAX - compressed_size ||
		    output_bytes > UINT64_MAX - block_bytes) {
			pr_err("Direct compressed I/O size overflows\n");
			return -1;
		}
		payload_bytes += compressed_size;
		output_bytes += block_bytes;
	}

	for (i = 0; i < piov->nr; i++) {
		if (iov_bytes > UINT64_MAX - piov->to[i].iov_len) {
			pr_err("Direct compressed destination size overflows\n");
			return -1;
		}
		iov_bytes += piov->to[i].iov_len;
	}

	if (payload_bytes != piov->b_layout.total_bytes ||
	    output_bytes != iov_bytes || piov->end < piov->from ||
	    payload_bytes != (uint64_t)(piov->end - piov->from) ||
	    piov->n_pages > UINT64_MAX / PAGE_SIZE ||
	    output_bytes != (uint64_t)piov->n_pages * PAGE_SIZE) {
		pr_err("Inconsistent direct compressed I/O sizes: payload=%" PRIu64
		       " metadata=%" PRIu64 " output=%" PRIu64 " iov=%" PRIu64
		       " range=%jd\n", payload_bytes,
		       piov->b_layout.total_bytes, output_bytes, iov_bytes,
		       (intmax_t)(piov->end - piov->from));
		return -1;
	}

	return 0;
}

static int transfer_async_block(const struct page_read_iov *piov,
				unsigned int *iov_index, size_t *iov_offset,
				const char *src, size_t bytes)
{
	while (bytes) {
		char *dst;
		size_t chunk;

		while (*iov_index < piov->nr &&
		       *iov_offset >= piov->to[*iov_index].iov_len) {
			*iov_offset -= piov->to[*iov_index].iov_len;
			(*iov_index)++;
		}
		if (*iov_index >= piov->nr) {
			pr_err("Async decompression ran out of destination iovecs\n");
			return -1;
		}

		dst = (char *)piov->to[*iov_index].iov_base + *iov_offset;
		chunk = piov->to[*iov_index].iov_len - *iov_offset;
		if (chunk > bytes)
			chunk = bytes;

		if (src) {
			memcpy(dst, src, chunk);
			src += chunk;
		} else {
			memset(dst, 0, chunk);
		}
		*iov_offset += chunk;
		bytes -= chunk;
	}

	return 0;
}

static int process_encoded_async_read(int fd, struct page_read_iov *piov,
				      struct encoded_read_ctx *ctx,
				      bool payload_ready,
				      struct encoded_prefetch *prefetch)
{
	struct decompress_job *jobs;
	char *compressed;
	char *scratch;
	size_t scratch_cap;
	size_t compressed_offset = 0;
	size_t total_compressed = piov->b_layout.total_bytes;
	size_t jobs_uncompressed = 0;
	size_t output_bytes = 0;
	size_t expected_output;
	size_t nr_jobs = 0;
	unsigned int iov_index = 0;
	size_t iov_offset = 0;
	bool parallel_zero = false;
	size_t i;
	int ret = -1;

	/*
	 * Materialize one encoded read into its destination iovecs. The caller
	 * holds the batch lease and serializes this parent-chain context; all
	 * reusable buffers remain context-owned until worker completion.
	 */
	if (!ctx || !ctx->batch_acquired) {
		pr_err("Encoded async I/O job has no active shared read context\n");
		return -1;
	}
	parallel_zero = compressed_restore_has_parallel_capacity(opts.decompress_threads);
	if (!piov->b_layout.nr_blocks || !piov->b_layout.sizes) {
		pr_err("Encoded async I/O job has no block metadata\n");
		return -1;
	}
	if (piov->b_layout.nr_blocks > (size_t)INT_MAX ||
	    piov->b_layout.nr_blocks > SIZE_MAX / sizeof(*jobs)) {
		pr_err("Encoded async I/O job has too many blocks: %zu\n",
		       piov->b_layout.nr_blocks);
		return -1;
	}
	if (piov->b_layout.pages_per_block && !piov->block_pages) {
		pr_err("Encoded block I/O job has no block-page metadata\n");
		return -1;
	}
	if ((uint64_t)total_compressed != piov->b_layout.total_bytes) {
		pr_err("Encoded async I/O size does not fit in size_t\n");
		return -1;
	}
	if (piov->n_pages > SIZE_MAX / PAGE_SIZE) {
		pr_err("Encoded async I/O page count does not fit in size_t\n");
		return -1;
	}
	expected_output = (size_t)piov->n_pages * (size_t)PAGE_SIZE;

	/*
	 * Resize reusable job and payload buffers before workers start. A ready
	 * payload was validated and installed by encoded_prefetch_take().
	 */
	if (ctx->jobs_cap < piov->b_layout.nr_blocks) {
		void *new_jobs = xrealloc(ctx->jobs,
					  piov->b_layout.nr_blocks * sizeof(*jobs));

		if (!new_jobs)
			goto out;
		ctx->jobs = new_jobs;
		ctx->jobs_cap = piov->b_layout.nr_blocks;
	}
	jobs = ctx->jobs;

	if (total_compressed) {
		if (ctx->compressed_cap < total_compressed) {
			void *new_compressed;

			if (payload_ready) {
				pr_err("Prefetched encoded payload exceeds its buffer\n");
				goto out;
			}
			new_compressed = xrealloc(ctx->compressed,
						  total_compressed);

			if (!new_compressed)
				goto out;
			ctx->compressed = new_compressed;
			ctx->compressed_cap = total_compressed;
		}
		compressed = ctx->compressed;
		if (!payload_ready &&
		    pread_full(fd, compressed, total_compressed, piov->from))
			goto out;
	} else
		compressed = NULL;
	scratch = ctx->scratch;
	scratch_cap = ctx->scratch_cap;

	/*
	 * Advance encoded and destination cursors together. Raw blocks and zero
	 * blocks without an eligible contiguous destination complete inline;
	 * contiguous zero and LZ4 blocks become disjoint jobs. Split LZ4 blocks
	 * use scratch because each job needs one contiguous destination.
	 */
	for (i = 0; i < piov->b_layout.nr_blocks; i++) {
		uint32_t compressed_size = piov->b_layout.sizes[i];
		unsigned int block_pages = 1;
		size_t block_bytes;
		size_t bound = PAGE_COMPRESSED_SIZE_BOUND;
		char *direct_dst = NULL;

		if (piov->b_layout.pages_per_block)
			block_pages = piov->block_pages[i];
		if (!block_pages ||
		    (piov->b_layout.pages_per_block && block_pages > piov->b_layout.pages_per_block)) {
			pr_err("Async: invalid page count %u for block %zu\n",
			       block_pages, i);
			goto out;
		}
		block_bytes = (size_t)block_pages * PAGE_SIZE;
		if (piov->b_layout.pages_per_block)
			bound = BLOCK_COMPRESSED_SIZE_BOUND(block_pages);
		if (compressed_size > bound ||
		    compressed_size > total_compressed - compressed_offset) {
			pr_err("Async: invalid compressed size %u for block %zu\n",
			       compressed_size, i);
			goto out;
		}
		if (output_bytes > SIZE_MAX - block_bytes) {
			pr_err("Async decompressed size overflows\n");
			goto out;
		}
		output_bytes += block_bytes;

		while (iov_index < piov->nr && iov_offset >= piov->to[iov_index].iov_len) {
			iov_offset -= piov->to[iov_index].iov_len;
			iov_index++;
		}
		if (iov_index >= piov->nr) {
			pr_err("Async: ran out of iovecs at block %zu\n", i);
			goto out;
		}
		if (block_bytes <= piov->to[iov_index].iov_len - iov_offset)
			direct_dst = (char *)piov->to[iov_index].iov_base + iov_offset;

		if (!compressed_size && (!direct_dst || !parallel_zero)) {
			if (transfer_async_block(piov, &iov_index, &iov_offset,
						 NULL, block_bytes))
				goto out;
		} else if (compressed_size == block_bytes) {
			if (transfer_async_block(piov, &iov_index, &iov_offset,
						 compressed + compressed_offset,
						 block_bytes))
				goto out;
		} else if (compressed_size >= block_bytes) {
			pr_err("Async: LZ4 block %zu has invalid size %u for %zu bytes\n",
			       i, compressed_size, block_bytes);
			goto out;
		} else if (direct_dst) {
			struct decompress_job *job = &jobs[nr_jobs++];

			job->src = compressed_size ? compressed + compressed_offset : NULL;
			job->dst = direct_dst;
			job->compressed_size = compressed_size;
			job->pages = block_pages;
			job->block_index = i;
			if (jobs_uncompressed > SIZE_MAX - block_bytes) {
				pr_err("Parallel decompression size overflows\n");
				goto out;
			}
			jobs_uncompressed += block_bytes;
			iov_offset += block_bytes;
		} else {
			void *new_scratch;

			if (scratch_cap < block_bytes) {
				new_scratch = xrealloc(scratch, block_bytes);
				if (!new_scratch)
					goto out;
				scratch = new_scratch;
				scratch_cap = block_bytes;
				ctx->scratch = scratch;
				ctx->scratch_cap = scratch_cap;
			}
			if (decompress_block(compressed + compressed_offset,
					     compressed_size, block_pages,
					     scratch)) {
				pr_err("Async decompression failed at split block %zu\n", i);
				goto out;
			}
			if (transfer_async_block(piov, &iov_index, &iov_offset,
						 scratch, block_bytes))
				goto out;
		}

		compressed_offset += compressed_size;
	}

	/* Require exact, single consumption of the payload and destination. */
	while (iov_index < piov->nr && iov_offset >= piov->to[iov_index].iov_len) {
		iov_offset -= piov->to[iov_index].iov_len;
		iov_index++;
	}
	if (compressed_offset != total_compressed || iov_index != piov->nr ||
	    iov_offset || output_bytes != expected_output) {
		pr_err("Inconsistent encoded async I/O sizes: payload=%zu/%zu output=%zu/%zu\n",
		       compressed_offset, total_compressed, output_bytes,
		       expected_output);
		goto out;
	}

	if (decompress_jobs_parallel(
		    cr_task_work_queue(), jobs, nr_jobs, jobs_uncompressed,
		    opts.decompress_threads,
		    prefetch ? encoded_prefetch_read : NULL, prefetch))
		goto out;

	ret = 0;
out:
	return ret;
}

static bool encoded_prefetch_eligible(const struct page_read *pr,
				      const struct page_read_iov *current,
				      const struct page_read_iov *next)
{
	size_t next_size = next->b_layout.total_bytes;

	if (opts.stream || pr->use_direct || next->storage != VMA_IO_ENCODED)
		return false;
	if (!compressed_restore_has_parallel_capacity(opts.decompress_threads))
		return false;
	if (current->n_pages < PARALLEL_COMPRESSED_MIN_BATCH_BYTES / PAGE_SIZE ||
	    next->n_pages < PARALLEL_COMPRESSED_MIN_BATCH_BYTES / PAGE_SIZE)
		return false;
	if (!next_size || next_size > ASYNC_BATCH_MAX_BYTES ||
	    (uint64_t)next_size != next->b_layout.total_bytes ||
	    next->end < next->from)
		return false;
	if ((uint64_t)(next->end - next->from) !=
	    next->b_layout.total_bytes)
		return false;

	return true;
}

int encoded_stream_read_batch(int fd, void *buf, const uint32_t *block_sizes,
			      unsigned long batch_pages, size_t batch_payload,
			      size_t nr_jobs, struct encoded_read_ctx *ctx,
			      unsigned long first_page_idx)
{
	size_t payload_offset = 0;
	size_t jobs_uncompressed = 0;
	size_t nr_jobs_built = 0;
	size_t i;
	ssize_t bytes;

	/* An all-zero batch has no encoded working set to allocate or acquire. */
	if (!batch_payload && !nr_jobs) {
		for (i = 0; i < batch_pages; i++) {
			if (block_sizes[i]) {
				pr_err("Compressed streaming batch metadata mismatch at page %lu\n", first_page_idx + i);
				return -1;
			}
			memset((char *)buf + i * PAGE_SIZE, 0, PAGE_SIZE);
		}
		return 0;
	}
	if (!ctx) {
		pr_err("Compressed streaming batch has no encoded-read context\n");
		return -1;
	}

	if (!ctx->batch_acquired)
		encoded_read_ctx_begin_work(ctx);
	if (ctx->compressed_cap < batch_payload) {
		void *new_compressed =
			xrealloc(ctx->compressed, batch_payload);

		if (!new_compressed)
			return -1;
		ctx->compressed = new_compressed;
		ctx->compressed_cap = batch_payload;
	}
	if (ctx->jobs_cap < nr_jobs) {
		void *new_jobs = xrealloc(ctx->jobs,
					  nr_jobs * sizeof(*ctx->jobs));

		if (!new_jobs)
			return -1;
		ctx->jobs = new_jobs;
		ctx->jobs_cap = nr_jobs;
	}

	if (batch_payload) {
		bytes = read_all(fd, ctx->compressed, batch_payload);
		if (bytes < 0) {
			pr_perror("Can't read compressed streaming batch");
			return -1;
		}
		if ((size_t)bytes != batch_payload) {
			pr_err("Reached EOF reading compressed streaming batch: %zd of %zu bytes\n",
			       bytes, batch_payload);
			return -1;
		}
	}

	for (i = 0; i < batch_pages; i++) {
		uint32_t cs = block_sizes[i];
		char *dst = (char *)buf + i * PAGE_SIZE;

		if (!cs) {
			memset(dst, 0, PAGE_SIZE);
		} else if (cs == PAGE_SIZE) {
			memcpy(dst, ctx->compressed + payload_offset, PAGE_SIZE);
		} else {
			struct decompress_job *job = &ctx->jobs[nr_jobs_built++];

			job->src = ctx->compressed + payload_offset;
			job->dst = dst;
			job->compressed_size = cs;
			job->pages = 1;
			job->block_index = i;
			jobs_uncompressed += PAGE_SIZE;
		}
		payload_offset += cs;
	}

	if (payload_offset != batch_payload || nr_jobs_built != nr_jobs) {
		pr_err("Compressed streaming batch metadata mismatch: %zu != %zu\n",
		       payload_offset, batch_payload);
		return -1;
	}
	if (nr_jobs &&
	    decompress_jobs_parallel(cr_task_work_queue(), ctx->jobs,
				     nr_jobs, jobs_uncompressed,
				     opts.decompress_threads, NULL, NULL)) {
		pr_err("Unable to decompress streaming batch at page %lu\n",
		       first_page_idx);
		return -1;
	}

	return 0;
}

int encoded_async_read_batch(int fd, struct page_read_iov *piov,
			     struct page_read_iov *next, bool is_last,
			     struct encoded_read_ctx *ctx,
			     const struct page_read *pr)
{
	bool prefetch_prepared = false;
	int payload_ready;
	int ret;

	if (!ctx) {
		pr_err("Encoded async I/O job has no shared read context\n");
		return -1;
	}
	if (!ctx->batch_acquired)
		encoded_read_ctx_begin_work(ctx);

	payload_ready = encoded_prefetch_take(ctx, piov, piov->b_layout.total_bytes);
	if (payload_ready < 0)
		return -1;
	if (!is_last && next && encoded_prefetch_eligible(pr, piov, next)) {
		prefetch_prepared = encoded_prefetch_prepare(
			ctx, fd, next->from,
			(size_t)next->b_layout.total_bytes);
	} else if (ctx->prefetch_batch_acquired) {
		encoded_prefetch_disable(ctx);
	}

	ret = process_encoded_async_read(
		fd, piov, ctx, payload_ready > 0,
		prefetch_prepared ? &ctx->prefetch : NULL);
	if (prefetch_prepared) {
		if (ret < 0 || !ctx->prefetch.complete)
			encoded_prefetch_disable(ctx);
		else
			encoded_prefetch_publish(ctx, next);
	}
	return ret;
}
