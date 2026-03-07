# Memory dumping and restoring

This article describes how CRIU dumps and restores processes' memory. For memory image file formats, see [memory dumps](memory-dumps.md).

## Basic C/R

### Dumping

Currently memory dumping depends on 3 big technologies:

- /proc/pid/smaps file and /proc/pid/map_files/ directory with links are used to determine
-* memory areas in use by task
-* mapped files (if any)
-* shared memory "identifier" to resolve the MAP_SHARED areas
- /proc/pid/pagemap file that reveals important flags
-* *present* indicates that the physical page is there. Non-present pages are not dumped.
-* *anonymoys* for the MAP_FILE | MAP_PRIVATE mapping indicate that the page in question is already COW-ed from the file's. Not-anonymous pages are not dumped as they are still in sync with the file
-* *soft-dirty* bit is used by [memory changes tracking](memory-changes-tracking.md)
- Ptrace SEIZE, used to grab pages from task's VM into a pipe (with vmsplice)

The last step deserves a more detailed explanation. In order to drain memory from a task, we first generate the bitmap of pages needed to be dumped (using the smaps, map_files and [pagemap cache](pagemap-cache.md) filled from proc). Next, we create a set of pipes to put pages into. Then we infect the process with [parasite code](parasite-code.md), which, in turn, gets the pipes and `vmsplice`s the required pages into it. Finally, we `splice` the pages from pipes into [image files](memory-dumps.md).

### Restoring

Restoring is pretty straightforward. During restore, CRIU morphs itself into a target task. Two things worth mentioning before diving into explanation of steps.

**[COW](cow.md)**
  Anonymous private mappings might have pages shared between tasks till they get COW-ed. To restore this CRIU pre-restores those pages before forking the child processes and `mremap`-s them in the [final stage](restorer-context.md).

**[Shared memory](shared-memory.md)**
  Those areas are implemented in the kernel by supporting a pseudo file on a hidden tmpfs mount. So on restore we just determine who will create the shared are and who will attach to it (see the [postulates](postulates.md)). Then the creator `mmap`-s the region and the others open the /proc/pid/map_files/ link. However, on the recent kernels, we use the new `memfd` system call that does similar thing but works for user namespaces. Briefly -- creator creates the memfd, all the others get one via /proc/pid/fd link which is not that strict as compared to the map_files.

Having said that, the restore of memory is done in the following steps:

**Open images and read in VMAs**
  Open all the mm.img, read mappings in, resolve shared memory segments and check whether we need to special-care mapped files.

**Fork and pre-mmap**
  Each task pre-mmaps private anonymous areas and populates them with pages (from pagemap/pages images). Then task forks the child which does the same. It is done in such way in order to make COWed areas actually share the pages they should. On fork() the shared pages become actually shared, as currently this is the only way to make Linux kernel do this.

**Open file mappings**
  Soon after fork we check which VMA-s are MAP_FILE ones and request the [files](files.md) engine to open them.

**Open shared mappings**
  At almost the same place we create an FD for shared anonymous VMA-s.

**Dive into [restorer context](restorer-context.md)**
  At this stage we strip off all the old CRIU mappings thus making the VM be ready for restored mappings.

**Restore mappings in their places**
  Anonymous private mappings are `mremap`-ed from the pre-mapped areas one-by-one, file mappings are created with `mmap` system call. Anonymous shared mappings are also just mmaped.

### Non linear mappings

Currently we don't support non-linear mappings (so dump fails if such mappings are found).

## Advanced C/R

For things as remote dump, stackable images, and incremental dumps, CRIU supports a more sophisticated memory C/R policies rather than "dump all -- restore all" one. There are several CLI knobs that can be used.

- dump action
- pre-dump action
- --track-mem option
-* --prev-images-dir option
- --leave-running option
- --page-server option

Let's see what all of this means.

First of all, the pre-dump action always turns on the `--track-mem` and the `--leave-running` options even if they are not specified in the command line. Next, the pre-dump action dumps *only* the memory, while the dump one dumps all the state including open files, sockets and other stuff. Having said that, let's see all the possible combinations and what they result in.

**dump**
  Without any options, dump everything and kill the dumped tasks.

**dump --track-mem**
  Dump everything, turn on memory changes tracking, and kill tasks after this. As you might have noticed, this is pretty useless combination of options!

**dump --leave-running**
  Dump everything, and leave the tasks running after dump.

**dump --track-mem --leave-running**
  Same as above, but turn on memory changes tracking.

**dump --track-mem --leave-running --prev-images-dir <path>**
  Same as above, but during dump also check whether the page in question is present in parent, and skip dumping it this time.

**pre-dump**
  Only dump memory, turn on memory changes tracking and leave the tasks running.

**pre-dump --prev-images-dir <path>**
  Same as above, but check for pages present in parent and skip them.

**<pre->dump <options> --page-server**
  Send the pages to the page server (e.g. for [disk-less migration](disk-less-migration.md)). See [page server](page-server.md) for more details.

## Messing with image files

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




