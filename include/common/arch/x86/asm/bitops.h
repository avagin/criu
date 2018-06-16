#ifndef __CR_ASM_BITOPS_H__
#define __CR_ASM_BITOPS_H__

#include "common/arch/x86/asm/cmpxchg.h"
#include "common/compiler.h"
#include "common/asm-generic/bitops.h"

static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
       int oldbit;

       asm volatile(LOCK_PREFIX "bts %2,%1\n\t"
                    "sbb %0,%0" : "=r" (oldbit), ADDR : "Ir" (nr) : "memory");

       return oldbit;
}

#endif /* __CR_ASM_BITOPS_H__ */
