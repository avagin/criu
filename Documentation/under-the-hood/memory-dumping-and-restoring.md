# Memory Dumping and Restoring

This article describes how CRIU dumps and restores process memory. For memory image file formats, see [Memory dumps](memory-dumps.md).

## Basic Checkpoint/Restore

### Dumping

Memory dumping currently relies on three key technologies:

- The `/proc/$pid/smaps` file and `/proc/$pid/map_files/` directory are used to determine:
    - Memory areas currently in use by a task.
    - Mapped files (if any).
    - Shared memory identifiers used to resolve `MAP_SHARED` areas.
- The `/proc/$pid/pagemap` file provides critical flags:
    - **Present**: Indicates the physical page is in memory. Only present pages are dumped.
    - **Anonymous**: For `MAP_FILE | MAP_PRIVATE` mappings, this indicates the page has been modified (COW) from the original file. Unmodified pages are not dumped as they can be recovered from the file.
    - **Soft-dirty**: Used for [memory changes tracking](memory-changes-tracking.md).
- `ptrace SEIZE` is used to extract pages from a task's virtual memory into a pipe using `vmsplice`.

The final step warrants a more detailed explanation. To drain memory from a task, CRIU first generates a bitmap of pages to be dumped (using `smaps`, `map_files`, and the [pagemap cache](pagemap-cache.md)). Next, a set of pipes is created. CRIU then injects [parasite code](parasite-code.md) into the process, which uses `vmsplice` to move the required pages into the pipes. Finally, CRIU `splice`s the pages from the pipes into [image files](memory-dumps.md).

### Restoring

During restoration, CRIU morphs itself into the target task. Two points are worth noting:

**[Copy-on-Write (COW)](cow.md)**
Anonymous private mappings may have pages shared between tasks until they are modified. To restore this, CRIU pre-restores these pages before forking child processes and uses `mremap` in the [final stage](restorer-context.md).

**[Shared Memory](shared-memory.md)**
Shared regions are implemented in the kernel via a pseudo-file on a hidden `tmpfs` mount. During restoration, CRIU determines which process will create the shared area and which will attach to it (see [Postulates](postulates.md)). The creator `mmap`s the region, and others open it via the `/proc/$pid/map_files/` link. On modern kernels, the `memfd` system call is used for similar functionality within user namespaces.

The memory restoration process follows these steps:

1. **Opening Images and Reading VMAs**: Open `mm.img`, read mappings, resolve shared memory segments, and identify mapped files requiring special handling.
1. **Forking and Pre-mmapping**: Each task pre-maps private anonymous areas and populates them with pages from the images. The task then forks a child, which performs the same operations. This ensures that COW areas correctly share pages, as `fork()` is the standard mechanism for the Linux kernel to establish this sharing.
1. **Opening File Mappings**: After forking, CRIU identifies `MAP_FILE` VMAs and uses the [files](files.md) engine to open them.
1. **Opening Shared Mappings**: CRIU creates file descriptors for shared anonymous VMAs.
1. **Entering the [Restorer Context](restorer-context.md)**: CRIU strips away its own original mappings, preparing the virtual memory for the restored mappings.
1. **Restoring Mappings**: Anonymous private mappings are `mremap`ed from pre-mapped areas, while file mappings and anonymous shared mappings are created via `mmap`.

### Non-linear Mappings

CRIU does not currently support non-linear mappings; the dump will fail if they are encountered.

## Advanced Checkpoint/Restore

For scenarios like remote dumping, stackable images, and incremental dumps, CRIU supports sophisticated memory policies beyond a simple "dump all/restore all." Several command-line options are available:

- `dump` action
- `pre-dump` action
- `--track-mem`
- `--prev-images-dir`
- `--leave-running`
- `--page-server`

The `pre-dump` action automatically enables `--track-mem` and `--leave-running`. While a `pre-dump` captures only memory, a full `dump` captures the entire state, including files and sockets. Common combinations include:

- **`dump`**: Dumps everything and terminates the tasks.
- **`dump --leave-running`**: Dumps everything and allows tasks to continue execution.
- **`dump --track-mem --leave-running --prev-images-dir <path>`**: Dumps only pages modified since the previous dump in `<path>`, while leaving tasks running.
- **`pre-dump`**: Dumps memory only, enables tracking, and leaves tasks running.
- **`pre-dump --prev-images-dir <path>`**: Performs an incremental memory dump.
- **`dump --page-server`**: Sends pages directly to a [page server](page-server.md) (e.g., for [disk-less migration](disk-less-migration.md)).

## Image File Workflow

![File:Criu-memory-wflow.png](File:Criu-memory-wflow.png)

## See also

- [Memory changes tracking](memory-changes-tracking.md)
- [Parasite code](parasite-code.md)
- [Memory dumps](memory-dumps.md)
- [COW](cow.md)
- [Shared memory](shared-memory.md)
- [Postulates](postulates.md)
- [Disk-less migration](disk-less-migration.md)
- [Page server](page-server.md)
