# Memory images deduplication

When performing [incremental dumps](incremental-dumps.md) or [iterative migration](iterative-migration.md), a layered stack of memory images is created. In that stack, some data is duplicated (i.e. same memory page is present in multiple images). This article describes ways to deduplicate such data by punching holes in image files (using `fallocate()` syscall with `FALLOC_FL_PUNCH_HOLE` flag), effectively freeing used disk space.

## Deduplication mode

Two ways to deduplicate memory images are available.

### Offline

The `criu dedup` command opens the image directory and punches holes in the *parent* images where *child* images would replace them.

### On the fly

The `--auto-dedup` option can be used for `criu dump`, `criu pre-dump` and `criu page-server`. It causes every write to images with process' pages to punch holes in the respective parent images, which is extremely useful in [disk-less migration](disk-less-migration.md) scenario.

The `--auto-dedup` option can also be used for `criu restore`. This makes CRIU to punch holes in images as memory is being restored. This should be used if images are stored on tmpfs (i.e. in RAM, see [disk-less migration](disk-less-migration.md)), as this way RAM usage is not growing.

## Shared memory deduplication
-Main article: [Shared memory](shared-memory.md)*

Deduplication only makes sense for incremental memory dumps. For now CRIU can only track changes, create incremental checkpoints and do dedup for anonymous memory. Changes tracking, increments and deduplication for shared memory is currently (August 2016) available in CRIU development branch.

## Implementation notes

Memory images are stored into two files: *pagemap* and *pages* (see [memory dumps](memory-dumps.md) for details). Note that the deduplication process does not change *pagemap* in any way, it only punches holes in *pages* image files.

Note that having a hole in an image file have totally different meaning that is in no way similar to the one of **in_parent** flag in *pagemap* entry (described in [memory dumps](memory-dumps.md)).

## See also

- [Memory dumps](memory-dumps.md)
- [Iterative migration](iterative-migration.md)
- [Incremental dumps](incremental-dumps.md)
- [Disk-less migration](disk-less-migration.md)
- [Page server](page-server.md)

## External links

- http://man7.org/linux/man-pages/man2/fallocate.2.html




