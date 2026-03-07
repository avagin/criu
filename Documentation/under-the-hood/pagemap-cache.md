# Pagemap cache

Checkpointing processes with large number of small virtual memory areas (VMAs) can lead to significant performance overhead. This overhead is primarily due to the frequent reading of VMA information from `/proc/[pid]/pagemap`, as described in [Memory dumps](memory-dumps.md). To mitigate this problem, CRIU uses a pagemap cache (pmc) to reduce the frequency of `pread` calls by caching information about VMAs.


