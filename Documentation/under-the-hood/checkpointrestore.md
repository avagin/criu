# Checkpoint/Restore

This page describes the overall design of the Checkpoint and Restore mechanisms in CRIU.

## Checkpoint

The checkpoint procedure relies heavily on the **/proc** filesystem, which is the primary source of information for CRIU.
The information gathered from `/proc` includes:

- File descriptor information (via **/proc/$pid/fd** and **/proc/$pid/fdinfo**).
- Pipe parameters.
- Memory maps (via **/proc/$pid/maps** and **/proc/$pid/map_files/**).
- And more.

The process dumper (hereafter referred to as the "dumper") performs the following steps during the checkpoint stage.

### Collecting and Freezing the Process Tree
The PID of the process group leader is obtained from the command line (`--tree` option). Using this PID, the dumper traverses the **/proc/$pid/task/** directory to collect threads and **/proc/$pid/task/$tid/children** to gather child processes recursively. During this traversal, tasks are stopped using the `ptrace` `PTRACE_SEIZE` command.

*See also: [Freezing the tree](freezing-the-tree.md)*

### Collecting and Dumping Task Resources
In this step, CRIU reads all available information about the collected tasks and writes it to dump files. Resources are obtained as follows:

1. Virtual Memory Areas (VMAs) are parsed from **/proc/$pid/smaps**, and mapped files are identified via **/proc/$pid/map_files** links.
1. File descriptor numbers are retrieved from **/proc/$pid/fd**.
1. Core task parameters, such as registers, are dumped using the `ptrace` interface and by parsing the **/proc/$pid/stat** entry.

CRIU then injects [parasite code](parasite-code.md) into the task via the `ptrace` interface. This occurs in two stages: first, a few bytes are injected at the task's current `CS:IP` to execute an `mmap` syscall. Once `ptrace` runs this syscall, enough memory is allocated for the full parasite code. The parasite code is then copied into the dumpee's address space, and `CS:IP` is updated to point to it.

From the parasite's context, CRIU gathers additional information, such as:
1. Credentials
1. Memory contents

### Cleanup

Once all resources (such as memory pages, which can only be written from within the dumpee's address space) have been dumped, CRIU uses the `ptrace` facility again to "cure" the dumpee by removing the parasite code and restoring the original instructions. CRIU then detaches from the tasks, allowing them to resume operation.

## Restore

The restoration procedure (the "restorer") involves CRIU morphing itself into the tasks it is restoring. This process consists of four high-level steps:

### Resolving Shared Resources

CRIU reads the image files to identify which processes share specific resources. Shared resources are typically restored by one process; others either inherit them during the second stage (e.g., sessions) or obtain them through other means. Examples include shared files sent via UNIX sockets with `SCM_RIGHTS` messages or shared memory areas restored via `memfd` file descriptors.

### Forking the Process Tree

CRIU calls `fork()` repeatedly to recreate the process tree. Note that threads are not restored at this stage, but rather in the fourth step.

### Restoring Basic Task Resources

In this stage, CRIU restores most resources, excluding:

1. Exact memory mapping locations
1. Timers
1. Credentials
1. Threads

The restoration of these four resource types is delayed until the final stage for the reasons described below. During this stage, CRIU opens files, prepares [namespaces](namespaces.md), maps and populates private memory areas, creates sockets, and performs operations like `chdir()` and `chroot()`.

### Switching to the Restorer Context, Finalizing Restoration, and Resuming

The restorer blob is necessary because as CRIU morphs into the target process, it must unmap its own memory and map the target's. Some code must remain in memory to perform these `munmap` and `mmap` operations. The restorer blob is a small piece of code positioned so that it does not intersect with either CRIU's or the target's memory mappings. At the end of the third stage, CRIU jumps into this blob to restore the memory maps.

In this context, timers are restored (to prevent them from firing prematurely), credentials are set (allowing privileged operations like `fork_with_pid`), and threads are recreated to avoid issues with memory layout changes.

*See also: [restorer context](restorer-context.md), [tree after restore](tree-after-restore.md).*
