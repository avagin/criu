#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <sys/types.h>

#include "zdtmtst.h"

#if defined(__x86_64__)

#include "cpuid.h"

const char *test_doc = "Test if AMX tile configuration (XFEATURE_XTILE_CFG) survives c/r";
const char *test_author = "Andrei Vagin <avagin@gmail.com>";

#define __aligned __attribute__((aligned(64)))

#define NUM_TILES	8
#define MAX_TILES	16
#define RESERVED_BYTES	14

struct tile_config {
	uint8_t palette_id;
	uint8_t start_row;
	uint8_t reserved[RESERVED_BYTES];
	uint16_t colsb[MAX_TILES];
	uint8_t rows[MAX_TILES];
} __aligned;

static inline void ldtilecfg(const void *cfg)
{
	asm volatile(".byte 0xc4, 0xe2, 0x78, 0x49, 0x00\n" : : "a"(cfg) : "memory");
}

static inline void sttilecfg(void *cfg)
{
	asm volatile(".byte 0xc4, 0xe2, 0x79, 0x49, 0x00\n" : : "a"(cfg) : "memory");
}

static inline void tilerelease(void)
{
	asm volatile(".byte 0xc4, 0xe2, 0x78, 0x49, 0xc0\n" : : : "memory");
}

static int verify_cpu(void)
{
	unsigned int eax, ebx, ecx, edx;

	/* Do we have xsave? */
	cpuid(1, &eax, &ebx, &ecx, &edx);
	if (!(ecx & (1u << 27)))
		return -1;

	/* Is AMX-TILE supported? (CPUID.(EAX=07H, ECX=0):EDX[24]) */
	cpuid_count(7, 0, &eax, &ebx, &ecx, &edx);
	if (!(edx & (1u << 24)))
		return -1;

	/* Is XTILECFG enabled in XCR0? (CPUID.(EAX=0DH, ECX=0):EAX[17]) */
	cpuid_count(0xd, 0, &eax, &ebx, &ecx, &edx);
	if (!(eax & (1u << 17)))
		return -1;

	return 0;
}

static int fpu_test(void)
{
	static struct tile_config cfg_in, cfg_out;
	int ret = 0;

	memset(&cfg_in, 0, sizeof(cfg_in));
	cfg_in.palette_id = 1;
	cfg_in.start_row = 0;

	/* Configure tile parameters within palette 1 limits */
	cfg_in.colsb[0] = 32;
	cfg_in.rows[0] = 8;
	cfg_in.colsb[1] = 64;
	cfg_in.rows[1] = 16;
	cfg_in.colsb[2] = 16;
	cfg_in.rows[2] = 4;
	cfg_in.colsb[3] = 48;
	cfg_in.rows[3] = 12;

	ldtilecfg(&cfg_in);

	test_daemon();
	test_waitsig();

	sttilecfg(&cfg_out);
	tilerelease();

	if (memcmp(&cfg_in, &cfg_out, sizeof(cfg_in)) != 0) {
		test_msg("AMX tile config mismatch\n");
		ret = -1;
	} else {
		test_msg("AMX tile config matches\n");
		ret = 0;
	}

	return ret;
}

static int bare_run(void)
{
	test_msg("Your cpu doesn't support AMX tilecfg, skipping\n");

	test_daemon();
	test_waitsig();

	return 0;
}

int main(int argc, char *argv[])
{
	int ret = 0;

	test_init(argc, argv);

	ret = verify_cpu() ? bare_run() : fpu_test();

	if (!ret)
		pass();
	else
		fail();

	return 0;
}

#else

int main(int argc, char *argv[])
{
	test_init(argc, argv);
	skip("Unsupported arch");
	return 0;
}

#endif
