#include <unistd.h>

#include "restorer.h"
#include "asm/restorer.h"

#include "uapi/std/syscall.h"
#include "log.h"
#include "asm/fpu.h"
#include "cpu.h"

int restore_nonsigframe_gpregs(UserRegsEntry *r)
{
	return 0;
}
