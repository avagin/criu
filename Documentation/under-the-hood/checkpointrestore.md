# Checkpoint/Restore

This page describes the overall design of how Checkpoint and Restore work in CRIU.

## Checkpoint

The checkpoint procedure relies heavily on **/proc** file system (it's a general place where criu takes all the information it needs).
The information gathered from /proc includes:

- Files descriptors information (via **/proc/$pid/fd** and **/proc/$pid/fdinfo**).
- Pipes parameters.
- Memory maps (via **/proc/$pid/maps** and **/proc/$pid/map_files/**).
- etc.

The process dumper (called a dumper below) does the following steps during the checkpoint stage.

### Collect process tree and freeze it
The **$pid** of a process group leader is obtained from the command line (`--tree` option). By using this **$pid** the dumper walks though **/proc/$pid/task/** directory collecting threads and through the **/proc/$pid/task/$tid/children** to gathers children recursively. While walking tasks are stopped using the `ptrace`'s `PTRACE_SEIZE` command.

-See also: [Freezing the tree](freezing-the-tree.md)*

### Collect tasks' resources and dump them
At this step CRIU reads all the information (it knows) about collected tasks and writes them to dump files. The resources are obtained via
1. VMAs areas are parsed from **/proc/$pid/smaps** and mapped files are read from **/proc/$pid/map_files** links
1. File descriptor numbers are read via **/proc/$pid/fd**
1. Core parameters of a task (such as registers and friends) are being dumped via ptrace interface and parsing **/proc/$pid/stat** entry.

Then CRIU injects a [parasite code](parasite-code.md) into a task via ptrace interface. This is done in two steps -- at first we inject only a few bytes for *mmap* syscall at CS:IP the task has at moment of seizing. Then ptrace allow us to run an injected syscall and we allocate enough memory for a parasite code chunk we need for dumping. After that the parasite code is copied into new place inside dumpee address space and CS:IP set respectively to point to our parasite code.

From parsite context CRIU does more information such as
1. Credentials
1. Contents of memory

### Cleanup

After everything dumped (such as memory pages, which can be written out only from inside dumpee address space) we use ptrace facility again and cure dumpee by dropping out all our parasite code and restoring original code. Then CRIU detaches from tasks and they continue to operate.

## Restore

The restore procedure (aka restorer) is done by CRIU morphing itself into the tasks it restores. On the top-level it consists of 4 steps

### Resolve shared resources

At this step CRIU reads in image files and finds out which processes share which resources. Later shared resources are restored by some one process and all the others either inherit one on the 2nd stage (like session) or obtain in some other way. The latter is, for example, shared files which are sent with SCM_CREDS messages via unix sockets, or shared memory areas that are restoring via `memfd` file descriptor.

### Fork the process tree

At this step CRIU calls fork() many times to re-created the processes needed to be restored. Note, that threads are not restored here, but on the 4th step.

### Restore basic tasks resources

Here CRIU restores all resources but

1. memory mappings exact location
1. timers
1. credentials
1. threads

The restoration of the above four types of resources are delayed till the last stage for the reasons described below. On this stage CRIU opens files, prepares [namespaces](namespaces.md), maps (and fills with data) private memory areas, creates sockets, calls chdir() and chroot() and doing some more.

### Switch to restorer context, restore the rest and continue

The reason for restorer blob is simple. Since criu morphs into the target process, it will have to unmap all its memory and put back the target one. While doing so, some code should exist in memory (the code doing the munmap and mmap). Therefore, the restorer blob is introduced. It's a small piece of code, that doesn't intersect with criu mappings AND target mappings. At the end of stage 2 criu jumps into this blob and restores the memory maps.

At the same place we restore timers not to make them fire too early, here we restore credentials to let criu do privileged operations (like fork-with-pid) and threads not to make them suffer from sudden memory layout change.

-See also: [restorer context](restorer-context.md), [tree after restore](tree-after-restore.md).*

