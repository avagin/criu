#include <unistd.h>

#include "restorer.h"
#include "asm/restorer.h"
#include "asm/string.h"

#include <compel/plugins/std/syscall.h>
#include "log.h"
#include <compel/asm/fpu.h>
#include "cpu.h"

int restore_nonsigframe_gpregs(UserArmRegsEntry *r)
{
	return 0;
}
