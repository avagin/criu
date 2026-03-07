# Memory Images Deduplication

When performing [incremental dumps](incremental-dumps.md) or [iterative migration](iterative-migration.md), a layered stack of memory images is created. In this stack, some data is duplicated (i.e., the same memory page is present in multiple images). This article describes how to deduplicate this data by "punching holes" in image files using the `fallocate()` system call with the `FALLOC_FL_PUNCH_HOLE` flag, which effectively frees disk space.

## Deduplication Modes

Two methods for deduplicating memory images are available.

### Offline Deduplication

The `criu dedup` command processes an image directory and punches holes in **parent** images where **child** images would otherwise replace them.

### On-the-Fly Deduplication

The `--auto-dedup` option can be used with `criu dump`, `criu pre-dump`, and `criu page-server`. This causes every write to a process's memory images to simultaneously punch holes in the respective parent images. This is particularly useful in [disk-less migration](disk-less-migration.md) scenarios.

The `--auto-dedup` option can also be used with `criu restore`. This causes CRIU to punch holes in the images as the memory is being restored. This is recommended if images are stored on `tmpfs` (i.e., in RAM), as it prevents RAM usage from growing unnecessarily during restoration.

## Shared Memory Deduplication

*Main article: [Shared memory](shared-memory.md)*

Deduplication is primarily relevant for incremental memory dumps. Currently, CRIU can track changes, create incremental checkpoints, and perform deduplication for anonymous memory. Support for change tracking and deduplication for shared memory is also available.

## Implementation Notes

Memory images are stored in two files: `pagemap` and `pages` (see [Memory dumps](memory-dumps.md) for details). Note that the deduplication process does not modify the `pagemap`; it only punches holes in the `pages` image files.

A hole in an image file has a different meaning than the `in_parent` flag in a `pagemap` entry (as described in [Memory dumps](memory-dumps.md)).

## See also

- [Memory dumps](memory-dumps.md)
- [Iterative migration](iterative-migration.md)
- [Incremental dumps](incremental-dumps.md)
- [Disk-less migration](disk-less-migration.md)
- [Page server](page-server.md)

## External links

- http://man7.org/linux/man-pages/man2/fallocate.2.html
