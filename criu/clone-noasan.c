#include <sched.h>
#include "restorer.h"
#include "sched.h"
#include "common/compiler.h"
#include "log.h"
#include "common/bug.h"

/*
 * ASan doesn't play nicely with clone if we use current stack for
 * child task. ASan puts local variables on the fake stack
 * to catch use-after-return bug:
 *         https://github.com/google/sanitizers/wiki/AddressSanitizerUseAfterReturn#algorithm
 *
 * So it's become easy to overflow this fake stack frame in cloned child.
 * We need a real stack for clone().
 *
 * To workaround this we add clone_noasan() not-instrumented wrapper for
 * clone(). Unfortunately we can't use __attribute__((no_sanitize_address))
 * for this because of bug in GCC > 6:
 *         https://gcc.gnu.org/bugzilla/show_bug.cgi?id=69863
 *
 * So the only way is to put this wrapper in separate non-instrumented file
 *
 * WARNING: When calling clone_noasan make sure your not sitting in a later
 * __restore__ phase where other tasks might be creating threads, otherwise
 * all calls to clone_noasan should be guarder with
 *
 * 	lock_last_pid
 *	clone_noasan
 *	... wait for process to finish ...
 *	unlock_last_pid
 *
 * This locking is not required if clone3() is available. lock_last_pid()
 * and unlock_last_pid() will be noops if clone3() is available.
 */
int clone_noasan(int (*fn)(void *), int flags, void *arg, pid_t pid)
{
	void *stack_ptr = (void *)round_down((unsigned long)&stack_ptr - 1024, 16);
	struct _clone_args c_args = {};
	long ret;

	BUG_ON((flags & CLONE_VM) && !(flags & CLONE_VFORK));
	/*
	 * Reserve some bytes for clone() internal needs
	 * and use as stack the address above this area.
	 */
	if (pid == 0)
		return clone(fn, stack_ptr, flags, arg);

	pr_debug("Creating process using clone3()\n");

	/*
	 * The simple stack as configured for clone() above those not work
	 * with clone3(). At the writing of this code it was not clear
	 * where the difference is. One difference between clone() and
	 * clone3() is that clone3() wants the stack address and the stack
	 * size. The kernel does (stack + stack_size) and uses that value
	 * as the stack pointer for the newly created process.
	 */

	/*
	 * The stack_size seems to be pretty big and in the mmap() it is
	 * even twice the size. By telling the kernel the stack is
	 * STACK_SIZE and mmap()ing it twice the size the restore actually
	 * works again. Any different combination did not work. This
	 * is not understood.
	 */

	/*
	 * clone() used to encode the information about child signals
	 * in the clone flags. With clone3() these values are now part
	 * of the clone_args structure.
	 */
	if (flags & 0xff) {
		c_args.exit_signal = flags & 0xff;
		flags = flags & ~0xff;
	}
	c_args.flags = flags;
	/* Set PID for only one PID namespace */
	c_args.set_tid = ptr_to_u64(&pid);
	c_args.set_tid_size = 1;
	ret = syscall(__NR_clone3, &c_args, sizeof(c_args));
	if (ret == 0) {
		exit(fn(arg));
	}
	return ret;
}
