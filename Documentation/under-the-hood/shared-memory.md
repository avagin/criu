# Shared Memory

Every process has one or more memory mappings representing regions of virtual memory. Some mappings are shared among multiple processes; these are referred to as shared anonymous (non-file-based) memory mappings. This article describes the intricacies of handling these mappings in CRIU.

## Checkpoint

During a checkpoint, CRIU identifies all shared mappings to ensure they are captured correctly.

CRIU calls `fstatat()` on each entry found in `/proc/$PID/map_files/` and records the *device:inode* pair returned. If multiple processes share the same *device:inode* pair, the mapping is marked as shared. This works because the kernel creates a hidden `tmpfs` file for shared anonymous mappings that is accessible via the `map_files` entry.

Dumping a shared mapping involves two steps:
1. Writing an entry into the process's `mm.img` file.
1. Storing the actual memory contents in `pagemap-shmem.img` and `pages.img`. (See [Memory dumps](memory-dumps.md) for details).

If different processes map different portions of a shared memory segment, CRIU collects the offsets and lengths from all involved processes to determine the total segment size and then aggregates the content.

## Restore

During restoration, CRIU uses the information gathered during the checkpoint to recreate the shared mappings.

Among the processes sharing a mapping, the one with the lowest PID (see [Postulates](postulates.md)) is designated the **creator**. The creator's responsibility is to obtain a file descriptor for the mapping, restore the data, and signal the other processes when it is ready. Other processes wait for this signal.

To obtain a file descriptor:
- If `memfd_create()` is available (Linux kernel v3.17+), it is used to create the mapping, followed by `ftruncate()` to set the correct size.
- If `memfd_create()` is unavailable, the creator calls `mmap()` to create the mapping and then opens the corresponding file in `/proc/self/map_files/` to obtain a descriptor. Note that `map_files` is unavailable for processes in user namespaces due to security restrictions.

Once the creator has the descriptor, it maps the region, copies the data from the dump using `memcpy()`, and then unmaps the region while keeping the descriptor open. It then uses `futex(FUTEX_WAKE)` to notify the waiting processes.

Other processes wait via `futex(FUTEX_WAIT)`, then open the creator's `/proc/$CREATOR_PID/fd/$FD` file to retrieve the shared descriptor. Finally, all processes (including the creator) call `mmap()` to establish their specific mapping of the segment and then close the shared descriptor.

## Change Tracking

For [iterative migration](iterative-migration.md), it is useful to track memory changes. Since CRIU v2.5, change tracking is supported for shared memory as well as anonymous memory. CRIU achieves this by scanning the pagemaps of all shared memory segment owners and performing a logical AND on the collected soft-dirty bits. This tracking also enables [memory images deduplication](memory-images-deduplication.md) for shared segments.

## Dumping Present Pages

When capturing shared memory, CRIU only dumps pages that are actually present or swapped, rather than the entire segment. This is done by checking the corresponding bits in the owners' pagemap entries. Additionally, a shared memory page may exist in the kernel but not be mapped to any specific process. CRIU identifies these pages using `mincore()` on the shared memory segment and ANDs this with the per-process bitmaps.

## See also
- [Memory dumping and restoring](memory-dumping-and-restoring.md)
- [Memory images deduplication](memory-images-deduplication.md)
