#include <unistd.h>
#include <fenv.h>

#include "zdtmtst.h"

// SIMD, gcc with Intel Core 2 Duo uses SSE2(4)
#define getmxcsr(x)	asm ("stmxcsr %0" : "=m" (x));
#define setmxcsr(x)	asm ("ldmxcsr %0" : "=m" (x));


#include <signal.h>
#include <stdio.h>

int main (int argc, char **argv)
{
	unsigned int mxcsr;
	int i;

	test_init(argc, argv);

	printf ("Old divByZero exception: 0x%08X\n", feenableexcept (FE_DIVBYZERO));
	printf ("Old invalid exception:   0x%08X\n", feenableexcept (FE_INVALID));
	printf ("New fp exception:		0x%08X\n", fegetexcept ());

	test_daemon();
	for (i  = 0; test_go(); i++){
		if (i % 2) {
			fedisableexcept(FE_DIVBYZERO);
			feenableexcept (FE_INVALID);
		} else {
			feenableexcept (FE_DIVBYZERO);
			fedisableexcept(FE_INVALID);
		}
		getmxcsr (mxcsr);
		if (i && ((!!(mxcsr & 0x200)) ^ (!!(i % 2)))) {
			printf ("MXCSR:   %d 0x%08X %x\n", i, mxcsr, mxcsr & 0x200);
			return 1;
		}
		if (i && !((!!(mxcsr & 0x80)) ^ (!!(i % 2)))) {
			printf ("MXCSR:   %d 0x%08X %x\n", i, mxcsr, mxcsr & 0x80);
			return 1;
		}
	}
	pass();
	return 0;
}
