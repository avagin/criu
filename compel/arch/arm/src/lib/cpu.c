#include <string.h>
#include <stdbool.h>

#include "uapi/compel/cpu.h"

#include "common/bitops.h"

#include "log.h"

#undef	LOG_PREFIX
#define LOG_PREFIX "cpu: "

static compel_cpuinfo_t rt_info;
static bool rt_info_done = false;

void compel_set_cpu_cap(compel_cpuinfo_t *info, unsigned int feature) { }
void compel_clear_cpu_cap(compel_cpuinfo_t *info, unsigned int feature) { }
int compel_test_cpu_cap(compel_cpuinfo_t *info, unsigned int feature) { return 0; }
int compel_cpuid(compel_cpuinfo_t *info) { return 0; }

bool cpu_has_feature(unsigned int feature)
{
	if (!rt_info_done) {
		compel_cpuid(&rt_info);
		rt_info_done = true;
	}
	return compel_test_cpu_cap(&rt_info, feature);
}
