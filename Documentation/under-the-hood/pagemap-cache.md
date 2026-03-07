# Pagemap Cache

Checkpointing processes with a large number of small virtual memory areas (VMAs) can lead to significant performance overhead. This is primarily due to the frequent reading of VMA information from `/proc/$pid/pagemap`, as described in [Memory dumps](memory-dumps.md). To mitigate this, CRIU utilizes a pagemap cache (PMC) that caches VMA information, thereby reducing the number of required `pread` calls.
