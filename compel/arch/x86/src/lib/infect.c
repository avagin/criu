#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/user.h>

#include <compel/asm/fpu.h>

#include "asm/cpu.h"

#include <compel/asm/processor-flags.h>
#include <compel/cpu.h>
#include "errno.h"
#include <compel/plugins/std/syscall-codes.h>
#include <compel/plugins/std/syscall.h>
#include "asm/ptrace.h"
#include "common/err.h"
#include "asm/infect-types.h"
#include "uapi/compel/ptrace.h"
#include "infect.h"
#include "infect-priv.h"
#include "log.h"

/*
 * Injected syscall instruction
 */
const char code_syscall[] = {
	0x0f, 0x05,				/* syscall    */
	0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc	/* int 3, ... */
};

const char code_int_80[] = {
	0xcd, 0x80,				/* int $0x80  */
	0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc	/* int 3, ... */
};

static const int
code_syscall_aligned = round_up(sizeof(code_syscall), sizeof(long));
static const int
code_int_80_aligned = round_up(sizeof(code_syscall), sizeof(long));

static inline __always_unused void __check_code_syscall(void)
{
	BUILD_BUG_ON(code_int_80_aligned != BUILTIN_SYSCALL_SIZE);
	BUILD_BUG_ON(code_syscall_aligned != BUILTIN_SYSCALL_SIZE);
	BUILD_BUG_ON(!is_log2(sizeof(code_syscall)));
}

#define get_signed_user_reg(pregs, name)				\
	((user_regs_native(pregs)) ? (int64_t)((pregs)->native.name) :	\
				(int32_t)((pregs)->compat.name))

int compel_get_task_regs(pid_t pid, user_regs_struct_t regs, save_regs_t save, void *arg)
{
	user_fpregs_struct_t xsave	= {  }, *xs = NULL;

	struct iovec iov;
	int ret = -1;

	pr_info("Dumping general registers for %d in %s mode\n", pid,
			user_regs_native(&regs) ? "native" : "compat");

	/* Did we come from a system call? */
	if (get_signed_user_reg(&regs, orig_ax) >= 0) {
		/* Restart the system call */
		switch (get_signed_user_reg(&regs, ax)) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			set_user_reg(&regs, ax, get_user_reg(&regs, orig_ax));
			set_user_reg(&regs, ip, get_user_reg(&regs, ip) - 2);
			break;
		case -ERESTART_RESTARTBLOCK:
			pr_warn("Will restore %d with interrupted system call\n", pid);
			set_user_reg(&regs, ax, -EINTR);
			break;
		}
	}

#ifndef PTRACE_GETREGSET
# define PTRACE_GETREGSET 0x4204
#endif

	if (!cpu_has_feature(X86_FEATURE_FPU))
		goto out;

	/*
	 * FPU fetched either via fxsave or via xsave,
	 * thus decode it accrodingly.
	 */

	pr_info("Dumping GP/FPU registers for %d\n", pid);

	if (cpu_has_feature(X86_FEATURE_XSAVE)) {
		iov.iov_base = &xsave;
		iov.iov_len = sizeof(xsave);

		if (ptrace(PTRACE_GETREGSET, pid, (unsigned int)NT_X86_XSTATE, &iov) < 0) {
			pr_perror("Can't obtain FPU registers for %d", pid);
			goto err;
		}
	} else {
		if (ptrace(PTRACE_GETFPREGS, pid, NULL, &xsave)) {
			pr_perror("Can't obtain FPU registers for %d", pid);
			goto err;
		}
	}

	xs = &xsave;
out:
	ret = save(arg, &regs, xs);
err:
	return ret;
}

int compel_syscall(struct parasite_ctl *ctl, int nr, unsigned long *ret,
		unsigned long arg1,
		unsigned long arg2,
		unsigned long arg3,
		unsigned long arg4,
		unsigned long arg5,
		unsigned long arg6)
{
	user_regs_struct_t regs = ctl->orig.regs;
	int err;

	if (user_regs_native(&regs)) {
		user_regs_struct64 *r = &regs.native;

		r->ax  = (uint64_t)nr;
		r->di  = arg1;
		r->si  = arg2;
		r->dx  = arg3;
		r->r10 = arg4;
		r->r8  = arg5;
		r->r9  = arg6;

		err = compel_execute_syscall(ctl, &regs, code_syscall);
	} else {
		user_regs_struct32 *r = &regs.compat;

		r->ax  = (uint32_t)nr;
		r->bx  = arg1;
		r->cx  = arg2;
		r->dx  = arg3;
		r->si  = arg4;
		r->di  = arg5;
		r->bp  = arg6;

		err = compel_execute_syscall(ctl, &regs, code_int_80);
	}

	*ret = get_user_reg(&regs, ax);
	return err;
}

void *remote_mmap(struct parasite_ctl *ctl,
		  void *addr, size_t length, int prot,
		  int flags, int fd, off_t offset)
{
	unsigned long map;
	int err;
	bool compat_task = !user_regs_native(&ctl->orig.regs);

	err = compel_syscall(ctl, __NR(mmap, compat_task), &map,
			(unsigned long)addr, length, prot, flags, fd, offset);
	if (err < 0)
		return NULL;

	if (IS_ERR_VALUE(map)) {
		if (map == -EACCES && (prot & PROT_WRITE) && (prot & PROT_EXEC))
			pr_warn("mmap(PROT_WRITE | PROT_EXEC) failed for %d, "
				"check selinux execmem policy\n", ctl->rpid);
		return NULL;
	}

	return (void *)map;
}

/*
 * regs must be inited when calling this function from original context
 */
void parasite_setup_regs(unsigned long new_ip, void *stack, user_regs_struct_t *regs)
{
	set_user_reg(regs, ip, new_ip);
	if (stack)
		set_user_reg(regs, sp, (unsigned long) stack);

	/* Avoid end of syscall processing */
	set_user_reg(regs, orig_ax, -1);

	/* Make sure flags are in known state */
	set_user_reg(regs, flags, get_user_reg(regs, flags) &
			~(X86_EFLAGS_TF | X86_EFLAGS_DF | X86_EFLAGS_IF));
}

#define USER32_CS	0x23
#define USER_CS		0x33

static bool ldt_task_selectors(pid_t pid)
{
	unsigned long cs;

	errno = 0;
	/*
	 * Offset of register must be from 64-bit set even for
	 * compatible tasks. Fix this to support native i386 tasks
	 */
	cs = ptrace(PTRACE_PEEKUSER, pid, offsetof(user_regs_struct64, cs), 0);
	if (errno != 0) {
		pr_perror("Can't get CS register for %d", pid);
		return -1;
	}

	return cs != USER_CS && cs != USER32_CS;
}

static int arch_task_compatible(pid_t pid)
{
	user_regs_struct_t r;
	int ret = ptrace_get_regs(pid, &r);

	if (ret)
		return -1;

	return !user_regs_native(&r);
}

bool arch_can_dump_task(struct parasite_ctl *ctl)
{
	pid_t pid = ctl->rpid;
	int ret;

	ret = arch_task_compatible(pid);
	if (ret < 0)
		return false;

	if (ret && !(ctl->ictx.flags & INFECT_HAS_COMPAT_SIGRETURN)) {
		pr_err("Can't dump task %d running in 32-bit mode\n", pid);
		return false;
	}

	if (ldt_task_selectors(pid)) {
		pr_err("Can't dump task %d with LDT descriptors\n", pid);
		return false;
	}

	return true;
}

/* Copied from the gdb header gdb/nat/x86-dregs.h */

/* Debug registers' indices.  */
#define DR_FIRSTADDR 0
#define DR_LASTADDR  3
#define DR_NADDR     4  /* The number of debug address registers.  */
#define DR_STATUS    6  /* Index of debug status register (DR6).  */
#define DR_CONTROL   7  /* Index of debug control register (DR7).  */

#define DR_LOCAL_ENABLE_SHIFT   0 /* Extra shift to the local enable bit.  */
#define DR_GLOBAL_ENABLE_SHIFT  1 /* Extra shift to the global enable bit.  */
#define DR_ENABLE_SIZE          2 /* Two enable bits per debug register.  */

/* Locally enable the break/watchpoint in the I'th debug register.  */
#define X86_DR_LOCAL_ENABLE(i) (1 << (DR_LOCAL_ENABLE_SHIFT + DR_ENABLE_SIZE * (i)))

int ptrace_set_breakpoint(pid_t pid, void *addr)
{
	int ret;

	/* Set a breakpoint */
	if (ptrace(PTRACE_POKEUSER, pid,
			offsetof(struct user, u_debugreg[DR_FIRSTADDR]),
			addr)) {
		pr_perror("Unable to setup a breakpoint into %d", pid);
		return -1;
	}

	/* Enable the breakpoint */
	if (ptrace(PTRACE_POKEUSER, pid,
			offsetof(struct user, u_debugreg[DR_CONTROL]),
			X86_DR_LOCAL_ENABLE(DR_FIRSTADDR))) {
		pr_perror("Unable to enable the breakpoint for %d", pid);
		return -1;
	}

	ret = ptrace(PTRACE_CONT, pid, NULL, NULL);
	if (ret) {
		pr_perror("Unable to restart the  stopped tracee process %d", pid);
		return -1;
	}

	return 1;
}

int ptrace_flush_breakpoints(pid_t pid)
{
	/* Disable the breakpoint */
	if (ptrace(PTRACE_POKEUSER, pid,
			offsetof(struct user, u_debugreg[DR_CONTROL]),
			0)) {
		pr_perror("Unable to disable the breakpoint for %d", pid);
		return -1;
	}

	return 0;
}

int ptrace_get_regs(pid_t pid, user_regs_struct_t *regs)
{
	struct iovec iov;
	int ret;

	iov.iov_base = &regs->native;
	iov.iov_len = sizeof(user_regs_struct64);

	ret = ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov);
	if (ret == -1) {
		pr_perror("PTRACE_GETREGSET failed");
		return -1;
	}

	if (iov.iov_len == sizeof(regs->native)) {
		regs->__is_native = NATIVE_MAGIC;
		return ret;
	}
	if (iov.iov_len == sizeof(regs->compat)) {
		regs->__is_native = COMPAT_MAGIC;
		return ret;
	}

	pr_err("PTRACE_GETREGSET read %zu bytes for pid %d, but native/compat regs sizes are %zu/%zu bytes",
			iov.iov_len, pid,
			sizeof(regs->native), sizeof(regs->compat));
	return -1;
}

int ptrace_set_regs(pid_t pid, user_regs_struct_t *regs)
{
	struct iovec iov;

	if (user_regs_native(regs)) {
		iov.iov_base = &regs->native;
		iov.iov_len = sizeof(user_regs_struct64);
	} else {
		iov.iov_base = &regs->compat;
		iov.iov_len = sizeof(user_regs_struct32);
	}
	return ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &iov);
}
