#ifndef _LINUX_TASKDIAG_H
#define _LINUX_TASKDIAG_H

#include <linux/types.h>
#include <linux/capability.h>

enum {
	/* optional attributes which can be specified in show_flags */
	TASK_DIAG_MSG	= 0,
	TASK_DIAG_CRED,
	TASK_DIAG_STAT,
	TASK_DIAG_VMA,

	/* other attributes */
	TASK_DIAG_PID	= 64,
	TASK_DIAG_VMA_NAME,
};

#define TASK_DIAG_SHOW_MSG	(1ULL << TASK_DIAG_MSG)
#define TASK_DIAG_SHOW_CRED	(1ULL << TASK_DIAG_CRED)
#define TASK_DIAG_SHOW_STAT	(1ULL << TASK_DIAG_STAT)
#define TASK_DIAG_SHOW_VMA	(1ULL << TASK_DIAG_VMA)

enum {
	TASK_DIAG_RUNNING,
	TASK_DIAG_INTERRUPTIBLE,
	TASK_DIAG_UNINTERRUPTIBLE,
	TASK_DIAG_STOPPED,
	TASK_DIAG_TRACE_STOP,
	TASK_DIAG_DEAD,
	TASK_DIAG_ZOMBIE,
};

#define TASK_DIAG_COMM_LEN 16

struct task_diag_msg {
	__u32	tgid;
	__u32	pid;
	__u32	ppid;
	__u32	tpid;
	__u32	sid;
	__u32	pgid;
	__u8	state;
	char	comm[TASK_DIAG_COMM_LEN];
};

struct task_diag_caps {
	__u32 cap[_LINUX_CAPABILITY_U32S_3];
};

struct task_diag_creds {
	struct task_diag_caps cap_inheritable;
	struct task_diag_caps cap_permitted;
	struct task_diag_caps cap_effective;
	struct task_diag_caps cap_bset;

	__u32 uid;
	__u32 euid;
	__u32 suid;
	__u32 fsuid;
	__u32 gid;
	__u32 egid;
	__u32 sgid;
	__u32 fsgid;
};

#define TASK_DIAG_VMA_F_READ		(1ULL <<  0)
#define TASK_DIAG_VMA_F_WRITE		(1ULL <<  1)
#define TASK_DIAG_VMA_F_EXEC		(1ULL <<  2)
#define TASK_DIAG_VMA_F_SHARED		(1ULL <<  3)
#define TASK_DIAG_VMA_F_MAYREAD		(1ULL <<  4)
#define TASK_DIAG_VMA_F_MAYWRITE	(1ULL <<  5)
#define TASK_DIAG_VMA_F_MAYEXEC		(1ULL <<  6)
#define TASK_DIAG_VMA_F_MAYSHARE	(1ULL <<  7)
#define TASK_DIAG_VMA_F_GROWSDOWN	(1ULL <<  8)
#define TASK_DIAG_VMA_F_PFNMAP		(1ULL <<  9)
#define TASK_DIAG_VMA_F_DENYWRITE	(1ULL << 10)
#define TASK_DIAG_VMA_F_MPX		(1ULL << 11)
#define TASK_DIAG_VMA_F_LOCKED		(1ULL << 12)
#define TASK_DIAG_VMA_F_IO		(1ULL << 13)
#define TASK_DIAG_VMA_F_SEQ_READ	(1ULL << 14)
#define TASK_DIAG_VMA_F_RAND_READ	(1ULL << 15)
#define TASK_DIAG_VMA_F_DONTCOPY	(1ULL << 16)
#define TASK_DIAG_VMA_F_DONTEXPAND	(1ULL << 17)
#define TASK_DIAG_VMA_F_ACCOUNT		(1ULL << 18)
#define TASK_DIAG_VMA_F_NORESERVE	(1ULL << 19)
#define TASK_DIAG_VMA_F_HUGETLB		(1ULL << 20)
#define TASK_DIAG_VMA_F_ARCH_1		(1ULL << 21)
#define TASK_DIAG_VMA_F_DONTDUMP	(1ULL << 22)
#define TASK_DIAG_VMA_F_SOFTDIRTY	(1ULL << 23)
#define TASK_DIAG_VMA_F_MIXEDMAP	(1ULL << 24)
#define TASK_DIAG_VMA_F_HUGEPAGE	(1ULL << 25)
#define TASK_DIAG_VMA_F_NOHUGEPAGE	(1ULL << 26)
#define TASK_DIAG_VMA_F_MERGEABLE	(1ULL << 27)

struct task_diag_vma {
	__u64 start, end;
	__u64 vm_flags;
	__u64 pgoff;
	__u32 major;
	__u32 minor;
	__u64 inode;
};

#define TASK_DIAG_DUMP_ALL	0
#define TASK_DIAG_DUMP_CHILDREN	1
#define TASK_DIAG_DUMP_THREAD	2
#define TASK_DIAG_DUMP_ONE	3

struct task_diag_pid {
	__u64	show_flags;
	__u64	dump_stratagy;

	__u32	pid;
};

enum {
	TASKDIAG_CMD_ATTR_UNSPEC = 0,
	TASKDIAG_CMD_ATTR_GET,
	__TASKDIAG_CMD_ATTR_MAX,
};

#define TASKDIAG_CMD_ATTR_MAX (__TASKDIAG_CMD_ATTR_MAX - 1)

#endif /* _LINUX_TASKDIAG_H */
