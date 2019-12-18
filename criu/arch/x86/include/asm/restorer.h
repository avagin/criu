#ifndef __CR_ASM_RESTORER_H__
#define __CR_ASM_RESTORER_H__

#include "asm/types.h"
#include <compel/asm/fpu.h>
#include "images/core.pb-c.h"
#include <compel/plugins/std/syscall-codes.h>
#include <compel/asm/sigframe.h>
#include "asm/compat.h"

#ifdef CONFIG_COMPAT
extern void restore_tls(tls_t *ptls);
extern int arch_compat_rt_sigaction(void *stack32, int sig,
		rt_sigaction_t_compat *act);
extern int set_compat_robust_list(uint32_t head_ptr, uint32_t len);
#else /* CONFIG_COMPAT */
static inline void restore_tls(tls_t *ptls) { }
static inline int arch_compat_rt_sigaction(void *stack, int sig, void *act)
{
	return -1;
}
static inline int set_compat_robust_list(uint32_t head_ptr, uint32_t len)
{
	return -1;
}
#endif /* !CONFIG_COMPAT */

/*
 * Documentation copied from glibc sysdeps/unix/sysv/linux/x86_64/clone.S
 * The kernel expects:
 * rax: system call number
 * rdi: flags
 * rsi: child_stack
 * rdx: TID field in parent
 * r10: TID field in child
 * r8:	thread pointer
 *
 * int clone(unsigned long clone_flags, unsigned long newsp,
 *           int *parent_tidptr, int *child_tidptr,
 *           unsigned long tls);
 */

#define RUN_CLONE_RESTORE_FN(ret, clone_flags, new_sp, parent_tid,	\
			     thread_args, clone_restore_fn)		\
	asm volatile(							\
		     "clone_emul:				\n"	\
		     "movq %2, %%rsi				\n"	\
		     "subq $16, %%rsi				\n"	\
		     "movq %6, %%rdi				\n"	\
		     "movq %%rdi, 8(%%rsi)			\n"	\
		     "movq %5, %%rdi				\n"	\
		     "movq %%rdi, 0(%%rsi)			\n"	\
		     "movq %1, %%rdi				\n"	\
		     "movq %3, %%rdx				\n"	\
		     "movq %4, %%r10				\n"	\
		     "movl $"__stringify(__NR_clone)", %%eax	\n"	\
		     "syscall					\n"	\
									\
		     "testq %%rax,%%rax				\n"	\
		     "jz thread_run				\n"	\
									\
		     "movq %%rax, %0				\n"	\
		     "jmp clone_end				\n"	\
									\
		     "thread_run:				\n"	\
		     "xorq %%rbp, %%rbp				\n"	\
		     "popq %%rax				\n"	\
		     "popq %%rdi				\n"	\
		     "callq *%%rax				\n"	\
									\
		     "clone_end:				\n"	\
		     : "=r"(ret)					\
		     : "g"(clone_flags),				\
		       "g"(new_sp),					\
		       "g"(&parent_tid),				\
		       "g"(&thread_args[i].pid),			\
		       "g"(clone_restore_fn),				\
		       "g"(&thread_args[i])				\
		     : "rax", "rcx", "rdi", "rsi", "rdx", "r10", "r11", "memory")

#define RUN_CLONE3_RESTORE_FN(ret, c_args, new_sp, c_args_size,	\
			     thread_args, clone_restore_fn)		\
	asm volatile(							\
		     "clone3_emul:				\n"	\
		     "movq %2, %%rsi				\n"	\
		     "subq $16, %%rsi				\n"	\
		     "movq %6, %%rdi				\n"	\
		     "movq %%rdi, 8(%%rsi)			\n"	\
		     "movq %5, %%rdi				\n"	\
		     "movq %%rdi, 0(%%rsi)			\n"	\
		     "movq %1, %%rdi				\n"	\
		     /* update c_arg.stack */				\
		     "movq %%rsi, 0x28(%%rdi)			\n"	\
		     "movq %3, %%rsi				\n"	\
		     "movl $"__stringify(__NR_clone3)", %%eax	\n"	\
		     "syscall					\n"	\
									\
		     "testq %%rax,%%rax				\n"	\
		     "jz thread3_run				\n"	\
									\
		     "movq %%rax, %0				\n"	\
		     "jmp clone3_end				\n"	\
									\
		     "thread3_run:				\n"	\
		     "xorq %%rbp, %%rbp				\n"	\
		     "popq %%rax				\n"	\
		     "popq %%rdi				\n"	\
		     "callq *%%rax				\n"	\
									\
		     "clone3_end:				\n"	\
		     : "=r"(ret)					\
		     : "g"(&c_args),				\
		       "g"(new_sp),					\
		       "g"(c_args_size),				\
		       "g"(&thread_args[i].pid),			\
		       "g"(clone_restore_fn),				\
		       "g"(&thread_args[i])				\
		     : "rax", "rcx", "rdi", "rsi", "rdx", "r10", "r11", "memory")

#define ARCH_FAIL_CORE_RESTORE					\
	asm volatile(						\
		     "movq %0, %%rsp			    \n"	\
		     "movq 0, %%rax			    \n"	\
		     "jmp *%%rax			    \n"	\
		     :						\
		     : "r"(ret)					\
		     : "memory")

static inline void
__setup_sas_compat(struct ucontext_ia32* uc, ThreadSasEntry *sas)
{
	uc->uc_stack.ss_sp	= (compat_uptr_t)(sas)->ss_sp;
	uc->uc_stack.ss_flags	= (int)(sas)->ss_flags;
	uc->uc_stack.ss_size	= (compat_size_t)(sas)->ss_size;
}

static inline void
__setup_sas(struct rt_sigframe* sigframe, ThreadSasEntry *sas)
{
	if (sigframe->is_native) {
		struct rt_ucontext *uc	= &sigframe->native.uc;

		uc->uc_stack.ss_sp	= (void *)decode_pointer((sas)->ss_sp);
		uc->uc_stack.ss_flags	= (int)(sas)->ss_flags;
		uc->uc_stack.ss_size	= (size_t)(sas)->ss_size;
	} else {
		__setup_sas_compat(&sigframe->compat.uc, sas);
	}
}

static inline void _setup_sas(struct rt_sigframe* sigframe, ThreadSasEntry *sas)
{
	if (sas)
		__setup_sas(sigframe, sas);
}
#define setup_sas _setup_sas

int restore_gpregs(struct rt_sigframe *f, UserX86RegsEntry *r);
int restore_nonsigframe_gpregs(UserX86RegsEntry *r);

int ptrace_set_breakpoint(pid_t pid, void *addr);
int ptrace_flush_breakpoints(pid_t pid);

extern int arch_map_vdso(unsigned long map_at, bool compatible);

#endif
