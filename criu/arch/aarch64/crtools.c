#include <string.h>
#include <unistd.h>

#include <linux/elf.h>

#include "types.h"
#include <compel/asm/processor-flags.h>

#include <compel/asm/infect-types.h>
#include "asm/restorer.h"
#include "common/compiler.h"
#include <compel/ptrace.h>
#include "asm/dump.h"
#include "protobuf.h"
#include "images/core.pb-c.h"
#include "images/creds.pb-c.h"
#include "parasite-syscall.h"
#include "log.h"
#include "util.h"
#include "cpu.h"
#include "restorer.h"
#include "compel/infect.h"
#include "pstree.h"

#define assign_reg(dst, src, e) dst->e = (__typeof__(dst->e))(src)->e

static int save_pac_keys(int pid, CoreEntry *core)
{
	struct user_pac_address_keys paca;
	struct user_pac_generic_keys pacg;
	struct PacKeys *pac_entry;
	struct PacAddressKeys *pac_address_keys;
	struct PacGenericKeys *pac_generic_keys;
	long pac_enabled_key;
	struct iovec iov;
	int ret;

	iov.iov_base = &pac_enabled_key;
	iov.iov_len = sizeof(pac_enabled_key);
	if ((ret = ptrace(PTRACE_GETREGSET, pid, NT_ARM_PAC_ENABLED_KEYS, &iov))) {
		// FIXME: detecting whether PAC keys are supported or not should be done once.
		if (errno == EINVAL) {
			pr_perror("PAC keys are not supported\n");
			return 0;
		}
		pr_perror("Failed to obtain FPU registers for %d", pid);
		return -1;
	}

	pr_debug("%d: Dumping PAC keys\n", pid);
	pac_entry = xmalloc(sizeof(PacKeys));
	if (!pac_entry)
		return -1;
	core->ti_aarch64->pac_keys = pac_entry;
	pac_keys__init(pac_entry);
	pac_entry->pac_enabled_key = pac_enabled_key;

	iov.iov_base = &paca;
	iov.iov_len = sizeof(paca);
	if ((ret = ptrace(PTRACE_GETREGSET, pid, NT_ARM_PACA_KEYS, &iov))) {
		pr_perror("Failed to obtain FPU registers for %d", pid);
		return -1;
	}
	pac_address_keys = xmalloc(sizeof(PacAddressKeys));
	if (!pac_address_keys)
		return -1;
	pac_address_keys__init(pac_address_keys);
	pac_entry->pac_address_keys = pac_address_keys;
	pac_address_keys->apiakey_lo = paca.apiakey;
	pac_address_keys->apiakey_hi = paca.apiakey >> 64;
	pac_address_keys->apibkey_lo = paca.apibkey;
	pac_address_keys->apibkey_hi = paca.apibkey >> 64;
	pac_address_keys->apdakey_lo = paca.apdakey;
	pac_address_keys->apdakey_hi = paca.apdakey >> 64;
	pac_address_keys->apdbkey_lo = paca.apdbkey;
	pac_address_keys->apdbkey_hi = paca.apdbkey >> 64;

	iov.iov_base = &pacg;
	iov.iov_len = sizeof(pacg);
	if ((ret = ptrace(PTRACE_GETREGSET, pid, NT_ARM_PACG_KEYS, &iov))) {
		pr_perror("Failed to obtain FPU registers for %d", pid);
		return -1;
	}
	pac_generic_keys = xmalloc(sizeof(PacGenericKeys));
	if (!pac_generic_keys)
		return -1;
	pac_generic_keys__init(pac_generic_keys);
	pac_entry->pac_generic_keys = pac_generic_keys;
	pac_generic_keys->apgakey_lo = pacg.apgakey;
	pac_generic_keys->apgakey_hi = pacg.apgakey >> 64;

	return 0;
}

int save_task_regs(pid_t pid, void *x, user_regs_struct_t *regs, user_fpregs_struct_t *fpsimd)
{
	int i;
	CoreEntry *core = x;

	// Save the Aarch64 CPU state
	for (i = 0; i < 31; ++i)
		assign_reg(core->ti_aarch64->gpregs, regs, regs[i]);
	assign_reg(core->ti_aarch64->gpregs, regs, sp);
	assign_reg(core->ti_aarch64->gpregs, regs, pc);
	assign_reg(core->ti_aarch64->gpregs, regs, pstate);

	// Save the FP/SIMD state
	for (i = 0; i < 32; ++i) {
		core->ti_aarch64->fpsimd->vregs[2 * i] = fpsimd->vregs[i];
		core->ti_aarch64->fpsimd->vregs[2 * i + 1] = fpsimd->vregs[i] >> 64;
	}
	assign_reg(core->ti_aarch64->fpsimd, fpsimd, fpsr);
	assign_reg(core->ti_aarch64->fpsimd, fpsimd, fpcr);

	if (save_pac_keys(pid, core))
		return -1;
	return 0;
}

int arch_alloc_thread_info(CoreEntry *core)
{
	ThreadInfoAarch64 *ti_aarch64;
	UserAarch64RegsEntry *gpregs;
	UserAarch64FpsimdContextEntry *fpsimd;

	ti_aarch64 = xmalloc(sizeof(*ti_aarch64));
	if (!ti_aarch64)
		goto err;
	thread_info_aarch64__init(ti_aarch64);
	core->ti_aarch64 = ti_aarch64;

	gpregs = xmalloc(sizeof(*gpregs));
	if (!gpregs)
		goto err;
	user_aarch64_regs_entry__init(gpregs);

	gpregs->regs = xmalloc(31 * sizeof(uint64_t));
	if (!gpregs->regs)
		goto err;
	gpregs->n_regs = 31;

	ti_aarch64->gpregs = gpregs;

	fpsimd = xmalloc(sizeof(*fpsimd));
	if (!fpsimd)
		goto err;
	user_aarch64_fpsimd_context_entry__init(fpsimd);
	ti_aarch64->fpsimd = fpsimd;
	fpsimd->vregs = xmalloc(64 * sizeof(fpsimd->vregs[0]));
	fpsimd->n_vregs = 64;
	if (!fpsimd->vregs)
		goto err;

	return 0;
err:
	return -1;
}

void arch_free_thread_info(CoreEntry *core)
{
	if (CORE_THREAD_ARCH_INFO(core)) {
		if (CORE_THREAD_ARCH_INFO(core)->fpsimd) {
			xfree(CORE_THREAD_ARCH_INFO(core)->fpsimd->vregs);
			xfree(CORE_THREAD_ARCH_INFO(core)->fpsimd);
		}
		xfree(CORE_THREAD_ARCH_INFO(core)->gpregs->regs);
		xfree(CORE_THREAD_ARCH_INFO(core)->gpregs);
		xfree(CORE_THREAD_ARCH_INFO(core));
		CORE_THREAD_ARCH_INFO(core) = NULL;
	}
}

int restore_fpu(struct rt_sigframe *sigframe, CoreEntry *core)
{
	int i;
	struct fpsimd_context *fpsimd = RT_SIGFRAME_FPU(sigframe);

	if (core->ti_aarch64->fpsimd->n_vregs != 64)
		return 1;

	for (i = 0; i < 32; ++i)
		fpsimd->vregs[i] = (__uint128_t)core->ti_aarch64->fpsimd->vregs[2 * i] |
				   ((__uint128_t)core->ti_aarch64->fpsimd->vregs[2 * i + 1] << 64);
	assign_reg(fpsimd, core->ti_aarch64->fpsimd, fpsr);
	assign_reg(fpsimd, core->ti_aarch64->fpsimd, fpcr);

	fpsimd->head.magic = FPSIMD_MAGIC;
	fpsimd->head.size = sizeof(*fpsimd);

	return 0;
}

int restore_gpregs(struct rt_sigframe *f, UserRegsEntry *r)
{
#define CPREG1(d) f->uc.uc_mcontext.d = r->d

	int i;

	for (i = 0; i < 31; ++i)
		CPREG1(regs[i]);
	CPREG1(sp);
	CPREG1(pc);
	CPREG1(pstate);

#undef CPREG1

	return 0;
}

int arch_ptrace_restore(int pid, struct pstree_item *item)
{
	PacAddressKeys *paca;
	PacGenericKeys *pacg;
	long pac_enabled_keys;
	struct user_pac_address_keys upaca;
	struct user_pac_generic_keys upacg;
	struct iovec iov;
	int ret;

	if (!rsti(item)->arch_info.has_pac)
		return 0;

	pr_debug("%d: Restoring PAC keys\n", pid);

	paca = &rsti(item)->arch_info.pac_address_keys;
	pacg = &rsti(item)->arch_info.pac_generic_keys;
	pac_enabled_keys = rsti(item)->arch_info.pac_enabled_keys;

	upaca.apiakey = paca->apiakey_lo + ((__uint128_t)paca->apiakey_hi << 64);
	upaca.apibkey = paca->apibkey_lo + ((__uint128_t)paca->apibkey_hi << 64);
	upaca.apdakey = paca->apdakey_lo + ((__uint128_t)paca->apdakey_hi << 64);
	upaca.apdbkey = paca->apdbkey_lo + ((__uint128_t)paca->apdbkey_hi << 64);

	iov.iov_base = &upaca;
	iov.iov_len = sizeof(upaca);

	if ((ret = ptrace(PTRACE_SETREGSET, pid, NT_ARM_PACA_KEYS, &iov))) {
		pr_perror("Failed to obtain FPU registers for %d", pid);
		return 1;
	}

	upacg.apgakey = pacg->apgakey_lo + ((__uint128_t)pacg->apgakey_hi << 64);
	iov.iov_base = &upacg;
	iov.iov_len = sizeof(upacg);
	if ((ret = ptrace(PTRACE_SETREGSET, pid, NT_ARM_PACG_KEYS, &iov))) {
		pr_perror("Failed to obtain FPU registers for %d", pid);
		return 1;
	}

	iov.iov_base = &pac_enabled_keys;
	iov.iov_len = sizeof(pac_enabled_keys);
	if ((ret = ptrace(PTRACE_SETREGSET, pid, NT_ARM_PAC_ENABLED_KEYS, &iov))) {
		pr_perror("Failed to obtain FPU registers for %d", pid);
		return 1;
	}

	return 0;
}

void arch_rsti_init(struct pstree_item *current)
{
	PacKeys *pac_keys = current->core[0]->ti_aarch64->pac_keys;
	if (!pac_keys) {
		rsti(current)->arch_info.has_pac = false;
		return;
	}

	rsti(current)->arch_info.has_pac = true;
	rsti(current)->arch_info.pac_enabled_keys = pac_keys->pac_enabled_key;
	rsti(current)->arch_info.pac_address_keys = *pac_keys->pac_address_keys;
	rsti(current)->arch_info.pac_generic_keys = *pac_keys->pac_generic_keys;
}
