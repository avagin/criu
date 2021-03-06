#ifndef UAPI_COMPEL_CPU_H__
#define UAPI_COMPEL_CPU_H__

#include <stdbool.h>

#include <compel/asm/cpu.h>

extern void compel_set_cpu_cap(compel_cpuinfo_t *info, unsigned int feature);
extern void compel_clear_cpu_cap(compel_cpuinfo_t *info, unsigned int feature);
extern int compel_test_cpu_cap(compel_cpuinfo_t *info, unsigned int feature);
extern int compel_cpuid(compel_cpuinfo_t *info);
extern bool cpu_has_feature(unsigned int feature);

#endif /* UAPI_COMPEL_CPU_H__ */
