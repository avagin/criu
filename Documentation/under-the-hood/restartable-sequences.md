# Restartable Sequences

Restartable sequences (aka RSEQ) are short, carefully defined sections of user-space code that enable efficient access to per-CPU data structures without relying on heavyweight synchronization primitives such as mutexes or atomic operations.

Support for RSEQ was introduced in the Linux kernel in version 4.18, allowing user-space programs to register critical code paths that the kernel can safely restart when a CPU migration or preemption occurs. This mechanism enables high-performance, scalable data access patterns while preserving correctness. [The 5-year journey to bring restartable sequences to Linux](https://www.efficios.com/blog/2019/02/08/linux-restartable-sequences/) article provides more information about how restartable sequences work, their design, use cases, and kernel integration.

## Linux Kernel Interface for Restartable Sequences

The Linux kernel interface for RSEQ is intentionally minimal. It consists of a single system call:
`sys_rseq(struct rseq *rseq, uint32_t rseq_len, int flags, uint32_t sig)`

The full definition of the RSEQ data structures and related flags is provided in [include/uapi/linux/rseq.h](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/linux/rseq.h):

```

enum rseq_cs_flags {
	RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT = (1U << RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT_BIT),
	RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL = (1U << RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL_BIT),
	RSEQ_CS_FLAG_NO_RESTART_ON_MIGRATE = (1U << RSEQ_CS_FLAG_NO_RESTART_ON_MIGRATE_BIT),
};

struct rseq_cs {
	__u32 version; /* always 0 at this moment */
	enum rseq_cs_flags flags;
	void *start_ip;
	/* Offset from start_ip. */
	intptr_t post_commit_offset;
	void *abort_ip;
}

struct rseq {
	__u32 cpu_id_start;
	__u32 cpu_id;
	struct rseq_cs *rseq_cs;
	enum rseq_cs_flags flags;
}

```

From the userspace side, we need to keep `struct rseq` somewhere and register it on the kernel side using the `rseq` syscall.
Then, once we want to execute some code as a rseq critical section (`rseq cs` or just CS) we need to allocate and fill with the data
`struct rseq_cs`. We have to specify the start address of our CS, and the address of the abort handler (called when CS was interrupted by a preemption, migration
or signal). Then we need to put an pointer to `struct rseq_cs` to the `(struct rseq).rseq_cs` field.

## Handling of RSEQ Flags

Flags can be specified at either `struct rseq` and `struct rseq_cs` using values from `enum rseq_cs_flags`. Regardless of where they are set, the kernel combines them when determining restart behavior:

```

static int rseq_need_restart(struct task_struct *t, u32 cs_flags)
{
	u32 flags, event_mask;
	int ret;

	/* Get thread flags. */
	ret = get_user(flags, &t->rseq->flags);
	if (ret)
		return ret;

	/* Take critical section flags into account. */
	flags |= cs_flags; // <<<<<<<< here we have flags combined from struct rseq + struct rseq_cs

```

The most common `flags` value is zero. In this case, the restartable sequence critical section is interrupted whenever a preemption, CPU migration, or signal occurs, and the instruction pointer (IP) will be redirected to the abort handler. In some scenarios, however, applications may prefer to allow a critical section to complete even when certain events occur, and can do so by explicitly setting the appropriate flags.

Note that `RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL` must be used in conjunction with both `RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT` and `RSEQ_CS_FLAG_NO_RESTART_ON_MIGRATE`. The kernel enforces this constraint to prevent inconsistent restart semantics:
```

	/*
	 * Restart on signal can only be inhibited when restart on
	 * preempt and restart on migrate are inhibited too. Otherwise,
	 * a preempted signal handler could fail to restart the prior
	 * execution context on sigreturn.
	 */
	if (unlikely((flags & RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL) &&
		     (flags & RSEQ_CS_PREEMPT_MIGRATE_FLAGS) !=
		     RSEQ_CS_PREEMPT_MIGRATE_FLAGS))
		return -EINVAL;

```

## Checkpoint/Restore of RSEQ

CRIU handles restartable sequences differently depending on the execution state of the process at checkpoint time. This can be categorized into the following cases:

1. Process is not executing inside an rseq critical section
1. Process is executing inside an rseq critical section
## `flags` is `0`, `RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT`, or `RSEQ_CS_FLAG_NO_RESTART_ON_MIGRATE`
## `flags` includes `RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL`

### Executing outside critical section

This is the simplest case. The process has a `struct rseq` registered with the kernel, but its instruction pointer (IP) is not currently executing within an RSEQ critical section.

#### Checkpoint
CRIU only needs to locate the `struct rseq` instance and record its address, length, and signature. This information is obtained using the ptrace request `PTRACE_GET_RSEQ_CONFIGURATION` (see the `dump_thread_rseq` function).

#### Restore
During restore, CRIU retrieves the `struct rseq` information from the checkpoint image (see images/rseq.proto) and re-register it from the parasite context using the `rseq` syscall (see `restore_rseq` in `criu/pie/restorer.c`).

### Executing inside critical section

When a process is being checkpointed while its instruction pointer is inside an RSEQ critical section, CRIU preserves the instruction pointer exactly as it was at checkpoint time.
However, RSEQ semantics require that if execution of a critical section is interrupted, the kernel redirects execution to the associated abort handler. In particular, when the `flags` value is `0`, `RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT` or `RSEQ_CS_FLAG_NO_RESTART_ON_MIGRATE` the kernel automatically redirects the instruction pointer to the abort handler associated with the RSEQ critical section. As a result, restoring the process with its instruction pointer unchanged violates the RSEQ semantics, potentially leading to incorrect behavior or application crashes. To address this issue, CRIU explicitly adjusts the instruction pointer to match kernel behavior.

The logic responsible for this is implemented in the `fixup_thread_rseq` function:
```

if (task_in_rseq(rseq_cs, TI_IP(core))) {
	struct pid *tid = &item->threads[i];

...

	pr_warn("The %d task is in rseq critical section. IP will be set to rseq abort handler addr\n",
			tid->real);

...

	if (!(rseq_cs->flags & RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL)) {
		pr_warn("The %d task is in rseq critical section. IP will be set to rseq abort handler addr\n",
			tid->real);

		TI_IP(core) = rseq_cs->abort_ip;

		if (item->pid->real == tid->real) {
			compel_set_leader_ip(dmpi(item)->parasite_ctl, rseq_cs->abort_ip);
		} else {
			compel_set_thread_ip(dmpi(item)->thread_ctls[i], rseq_cs->abort_ip);
		}
	}
}

```

This code detects when a thread's instruction pointer lies within an RSEQ critical section and, unless `RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL` is set, rewrites the instruction pointer to the abort handler address. By doing so, CRIU mirrors the kernel's rseq fixup behavior and ensures that the restored process resumes execution in a semantically correct state.

#### Checkpoint
CRIU locates the `struct rseq` instance and records its address, length, and signature using the `PTRACE_GET_RSEQ_CONFIGURATION` ptrace request (see `dump_thread_rseq`).
In addition, the instruction pointer is explicitly adjusted to point to the RSEQ abort handler.

#### Restore
During restore, CRIU reads data about the `struct rseq` state from the checkpoint image (`images/rseq.proto`) and re-register it from the restorer context using the `rseq` system call (see `restore_rseq` in `criu/pie/restorer.c`). No further action is required: the process resumes execution at the abort handler, outside of the RSEQ critical section.

### Executing inside non-abortable critical section

This is a relatively rare case, but it is fully supported by CRIU. When an RSEQ critical section is marked with the `RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL` flag, it is effectively non-abortable.
At first glance, this might suggest that no special handling is required as the RSEQ structure could simply be saved, and the instruction pointer left unchanged. However, this assumption is incorrect.

During checkpoint, when CRIU transfers execution to the parasite, the kernel clears the `(struct rseq).rseq_cs` pointer if it determines that execution is no longer within an rseq critical section:
```

static int rseq_ip_fixup(struct pt_regs *regs)
{
...

	/*
	 * Handle potentially not being within a critical section.
	 * If not nested over a rseq critical section, restart is useless.
	 * Clear the rseq_cs pointer and return.
	 */
	if (!in_rseq_cs(ip, &rseq_cs))
		return clear_rseq_cs(t);

```

As a result, after restore, the process resumes execution at the correct instruction pointer within the critical section, but from the kernel's perspective it is no longer executing inside an RSEQ critical section. This discrepancy is problematic, because the kernel relies on the `(struct rseq).rseq_cs` field to determine rseq execution context.

#### Checkpoint

CRIU locates the `struct rseq` instance and records its address, length, and signature using the `PTRACE_GET_RSEQ_CONFIGURATION` ptrace request.
The instruction pointer is saved without modification, but the `(struct rseq).rseq_cs` field is also recorded in the CRIU image.

#### Restore

During restore, CRIU re-registers the `struct rseq` from the checkpoint image (`images/rseq.proto`) using the `rseq` system call from the restorer context (see `restore_rseq` in `criu/pie/restorer.c`). In addition, CRIU explicitly restores the `(struct rseq).rseq_cs` field using `PTRACE_POKEAREA` (see `restore_rseq_cs`) to reestablish the correct `rseq` execution context in the kernel.

## TODO

- tests for all architectures (right now we have ZDTM tests only for x86_64)
- improvement support of built-in rseq for non-Glibc libraries
- pre-dump tests (?)
- leave-running tests (?)
- crfail test
- threaded test

## Useful links

- [1] https://github.com/torvalds/linux/blob/b2d229d4ddb17db541098b83524d901257e93845/kernel/rseq.c#L1
- [2] https://www.efficios.com/blog/2019/02/08/linux-restartable-sequences/
- [3] https://lwn.net/Articles/883104/
- [4] https://patchwork.sourceware.org/project/glibc/list/?series=5530&state=*


