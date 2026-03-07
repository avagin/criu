# Restorer Context

This page explains the restorer context and its role in the restoration process.

## What is the Restorer Context?

The restorer context is the final stage of the [restoration](checkpointrestore.md) process. It is a specialized environment, similar to the [parasite code](parasite-code.md), that is compiled as a Position-Independent Executable (PIE) and operates without standard libraries. In this context, CRIU finalizes the restoration of:

1. Memory mappings
1. Timers
1. Credentials
1. Threads

## Why is a Separate Context Necessary?

A separate context is required because CRIU must eventually unmap its own memory mappings to make room for the target process's memory. Since CRIU is performing these operations on itself, it would segfault immediately upon exiting a `munmap()` call that removes its own code. To avoid this, a small "restorer blob" is used. This code is positioned in a memory "hole" that does not overlap with either CRIU's current mappings or the target process's intended mappings, allowing it to exist in both address spaces simultaneously.

The transition to this context involves several steps:

1. CRIU collects all data needed by the restorer and places it into a single sequential memory area.
1. CRIU identifies a suitable memory hole for the restorer code and data.
1. CRIU maps this region, moves the data into it, and places the restorer blob nearby.
1. Pointers within the restorer data are adjusted to be valid within the restorer's context.
1. CRIU executes an assembly "jump" instruction to transfer control to the restorer blob.

## What is Restored in This Context?

### Memory
Memory is restored here to avoid the self-unmapping issue mentioned above. At this stage, CRIU:
- Moves anonymous VMAs from their temporary locations to their final addresses using `mremap()`.
- Maps file-backed VMAs using `mmap()`.

### Timers
Timers are armed at the very last moment to ensure they do not fire prematurely while processes are still being synchronized during restoration.

### Credentials
Credentials are restored here to allow CRIU to perform its final privileged operations, such as `chdir()` or `chroot()`, just before the process resumes.

### Threads
Threads are restored in this final stage for simplicity. Restoring them earlier would require "parking" them during complex memory layout changes. Instead, CRIU completes the memory transition first and then recreates the threads.

## See also
- [Code blobs](code-blobs.md)
- [Compel](compel.md)
