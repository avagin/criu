# Shared memory

Every process has one or more memory mappings, i.e. regions of virtual memory it allows to use.
Some such mappings can be shared between a few processes, and they are called shared mappings.
In other words, these are shared **anonymous (not file-based) memory mappings**.
The article describes some intricacies of handling such mappings. 

## Checkpoint

During the checkpointing, CRIU needs to figure out all the shared mappings in order to dump them as such.

It does so by calling `fstatat()` on each entry found in the `/proc/$PID/map_files/`,
noting the *device:inode* pair of the structure returned by `fstatat()`. Now, if some processes
have a mapping with the same *device:inode* pair, this mapping is marked as shared between these processes
and dumped as such.

Note that `fstatat()` works because the kernel actually creates a hidden
tmpfs file, not visible from any tmpfs mounts, but accessible via its
`/proc/$PID/map_files/` entry.

Dumping a mapping means two things:
- writing an entry into process' mm.img file;
- storing the actual mapping data (contents).
For shared mappings, the contents is stored into a pair of image files: pagemap-shmem.img and pages.img.
For details, see [Memory dumps](memory-dumps.md).

Note that different processes can map different parts of a shared memory segment.
In this case, CRIU first collects mapping offsets and lengths from all the processes
to determine the total segment size, then reads all the parts contents
from the respective processes.

## Restore

During the restore, CRIU already knows which mappings are shared, so they need to be
restored as such. Here is how it is done.

Among all the processes sharing a mapping, the one with the lowest PID among the group
(see [postulates](postulates.md)) is assigned to be a mapping creator. The creator task is to obtain a mapping
file descriptor, restore the mapping data, and signal all the other process that it's ready.
During this process, all the other processes are waiting.

First, the creator need to obtain a file descriptor for the mapping. To achieve it, two different
approaches are used, depending on the availability.

In case [memfd_create()](http://man7.org/linux/man-pages/man2/memfd_create.2.html)
syscall is available (Linux kernel v3.17+), it is used to obtain a file descriptor.
Next, `ftruncate()` is called to set the proper size of mapping.

If `memfd_create()` is not available, the alternative approach is used.
First, mmap() is called to create a mapping. Next, a file in `/proc/self/map_files/`
is opened to get a file descriptor for the mapping. The limitation of this method is,
due to security concerns, /proc/$PID/map_files/ is not available for processes that
live inside a user namespace, so it is impossible to use it if there
are any user namespaces in the dump.

Once the creator have the file descriptor, it mmap()s it and restores its content from
the dump (using memcpy()). The creator then unmaps the the mapping (note the file
descriptor is still available). Next, it calls futex(FUTEX_WAKE) to signal all the
waiting processes that the mapping file descriptor is ready.

All the other processes that need this mapping wait on futex(FUTEX_WAIT). Once the
wait is over, they open the creator's /proc/$CREATOR_PID/fd/$FD file to get the
mapping file descriptor.

Finally, all the processes (including the creator itself) call mmap() to create a
needed mapping (note that mmap() arguments such as length, offset and flags may
differ for different processes), and close() the mapping file descriptor as it is
no longer needed.

## Changes tracking

For [iterative migration](iterative-migration.md) it's very useful to track changes in memory. Until CRIU v2.5, changes were tracked for anonymous memory only, but now it is also shared memory can be tracked as well. To achieve it, CRIU scans all the shmem segment owners' pagemap (as it does for anonymous memory) and then ANDs the collected soft-dirty bits.

The changes tracking caused developers to implement [memory images deduplication](memory-images-deduplication.md) for shmem segments as well.

## Dumping present pages

When dumping the contents of shared memory, CRIU does not dump all of the data. Instead, it determines which pages contain 
it, and only dumps those pages. This is done similarly to how regular [memory dumping and restoring](memory-dumping-and-restoring.md) works, i.e. by looking
for PRESENT or SWAPPED bits in owners' pagemap entries.

There is one particular feature of shared memory dumps worth mentioning. Sometimes, a shared memory page
can exist in the kernel, but it is not mapped to any process. CRIU detects such pages by calling mincore()
on the shmem segment, which reports back the page in-memory status. The mincore bitmap is when ANDed with
the per-process ones.

## See also

- [Memory dumping and restoring](memory-dumping-and-restoring.md)
- [Memory images deduplication](memory-images-deduplication.md)



