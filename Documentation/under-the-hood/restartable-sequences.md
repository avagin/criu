# Restartable Sequences

Restartable sequences (RSEQ) are short, carefully defined sections of userspace code that enable efficient access to per-CPU data structures without the overhead of heavyweight synchronization primitives like mutexes or atomic operations.

Introduced in Linux kernel version 4.18, RSEQ allows userspace programs to register critical code paths that the kernel can safely restart if a CPU migration or preemption occurs. This mechanism enables high-performance, scalable data access while ensuring correctness. For more background, see [The 5-year journey to bring restartable sequences to Linux](https://www.efficios.com/blog/2019/02/08/linux-restartable-sequences/).

## Linux Kernel Interface

The kernel interface for RSEQ is minimal, consisting of a single system call:
`sys_rseq(struct rseq *rseq, uint32_t rseq_len, int flags, uint32_t sig)`

The data structures and flags are defined in [`include/uapi/linux/rseq.h`](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/linux/rseq.h):

```c
enum rseq_cs_flags {
	RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT = (1U << RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT_BIT),
	RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL = (1U << RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL_BIT),
	RSEQ_CS_FLAG_NO_RESTART_ON_MIGRATE = (1U << RSEQ_CS_FLAG_NO_RESTART_ON_MIGRATE_BIT),
};

struct rseq_cs {
	__u32 version; /* always 0 currently */
	enum rseq_cs_flags flags;
	void *start_ip;
	intptr_t post_commit_offset;
	void *abort_ip;
};

struct rseq {
	__u32 cpu_id_start;
	__u32 cpu_id;
	struct rseq_cs *rseq_cs;
	enum rseq_cs_flags flags;
};
```

To use RSEQ, an application must maintain a `struct rseq` and register it with the kernel. Before entering an RSEQ critical section, the application populates a `struct rseq_cs` with the start address and an abort handler (to be called if the section is interrupted) and updates the `rseq_cs` pointer in the registered `struct rseq`.

## Handling RSEQ Flags

Flags can be specified in both `struct rseq` and `struct rseq_cs`. The kernel combines these flags to determine restart behavior. Typically, `flags` is zero, meaning the critical section is interrupted and the instruction pointer (IP) is redirected to the abort handler if preemption, migration, or a signal occurs. Applications can use specific flags to allow completion even during certain events.

Note that `RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL` must be used alongside both `RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT` and `RSEQ_CS_FLAG_NO_RESTART_ON_MIGRATE` to ensure consistent restart semantics.

## Checkpoint/Restore of RSEQ

CRIU handles RSEQ based on the process's execution state at checkpoint time:

### Case 1: Outside a Critical Section
This is the simplest scenario. The process has a registered `struct rseq`, but the IP is not within a critical section.

- **Checkpoint**: CRIU uses `PTRACE_GET_RSEQ_CONFIGURATION` to locate the `struct rseq` and record its address, length, and signature.
- **Restore**: CRIU retrieves the `struct rseq` information from the image and re-registers it from the restorer context.

### Case 2: Inside an Abortable Critical Section
If the IP is within an RSEQ critical section with standard flags (e.g., `0`), RSEQ semantics require that an interruption redirects the IP to the abort handler. Simply restoring the process with its original IP would violate these semantics.

- **Checkpoint**: CRIU records the `struct rseq` configuration and explicitly adjusts the saved IP to point to the RSEQ abort handler.
- **Restore**: The process resumes execution at the abort handler, ensuring a semantically correct state.

The `fixup_thread_rseq` function implements this logic by detecting if the IP falls within the registered `rseq_cs` range and rewriting it as needed.

### Case 3: Inside a Non-Abortable Critical Section
When `RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL` is set, the section is non-abortable. However, special handling is still required because the kernel clears the `rseq_cs` pointer when CRIU transfers execution to the parasite code.

- **Checkpoint**: CRIU saves the `struct rseq` configuration and the IP without modification, but also explicitly records the `rseq_cs` field.
- **Restore**: CRIU re-registers the `struct rseq` and then uses `PTRACE_POKEAREA` to restore the `rseq_cs` pointer, re-establishing the kernel's RSEQ execution context.

## TODO

- Test support for all architectures (currently x86_64 only).
- Improve support for built-in RSEQ in non-Glibc libraries.
- Add comprehensive tests for pre-dump, leave-running, and multi-threaded scenarios.

## Useful links
- https://github.com/torvalds/linux/blob/master/kernel/rseq.c
- https://www.efficios.com/blog/2019/02/08/linux-restartable-sequences/
- https://lwn.net/Articles/883104/
