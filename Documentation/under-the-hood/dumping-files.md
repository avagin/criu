# Dumping Files

This page describes how CRIU dumps information about open files.

## Files, Descriptors, and Inodes in Linux

When a task opens a file, the Linux kernel constructs a chain of three objects to manage it:

**Inode**
The Inode describes the file itself, including metadata (owner, type, size) and the actual data (the bytes on disk).

**Dentry (Directory Entry)**
A helper object the kernel uses to resolve a file path to an Inode. If a file has hard links, one Inode will have multiple Dentries.

**File**
The File object describes how a task interacts with an open Dentry-Inode pair (e.g., current offset, access mode).

**File Descriptor**
This is a number in the task's file descriptor table (FDT) that references a specific File object.

After an `open()` (or similar) system call, this chain exists in memory:

![File:Dumping_files-001.svg](File:Dumping_files-001.svg)

A File object can be referenced by more than one FDT. For example, when a task calls `fork()`, the child receives a new FDT that references the same File objects as the parent, making them shared objects.

![File:Dumping_files-002.svg](File:Dumping_files-002.svg)

Inodes are also noteworthy. In Linux, file descriptors can be obtained through `open()`, `pipe()`, `socket()`, and various Linux-specific calls like `epoll_create` or `signalfd`. In all these cases, the kernel creates the File-Dentry-Inode chain, but the type of Inode differs based on the call. CRIU understands these distinctions and handles them accordingly.

## How Information About Opened Files is Stored in CRIU

CRIU uses several image files to store information about open files.

### File Descriptors

The first image is `fdinfo-$id.img`, which contains information about a process's FDT. Entries include two critical fields: `fd` (the descriptor number) and `id` (an identifier for the File-Inode pair).

### Files and Inodes

To simplify matters, CRIU treats the File-Dentry-Inode triplet as a single object. Separate images are used for each Inode type:

- `reg-files.img`: Regular files (created via `open()`).
- `unixsk.img`: UNIX sockets.
- `pipes.img`: Pipes.
- `inetsk.img`: IP sockets (TCP and UDP).
- `signalfd.img`: Signal file descriptors.
- And others.

A full list of generated image files is available in [Images](images.md).

Each image stores the appropriate state for the File and Inode. Dentry information, specifically the file path, is primarily preserved for regular files.

## How CRIU Gathers Information for Dumping

During the dump process, CRIU must determine:

1. The FD numbers owned by tasks.
1. Which File objects are shared between task FDTs.
1. The types of Inodes involved.
1. The internal state of File and Inode objects.

### FD Numbers

This is straightforward: reading the `/proc/$pid/fd` or `/proc/$pid/fdinfo` directories reveals the required numbers.

### File Sharing

To determine if two FDs across different tasks refer to the same File object, CRIU uses the `kcmp` system call.

### Determining Inode Type

In most cases, the Inode type can be identified by calling `stat()` on the descriptor. CRIU achieves this by asking the [parasite code](parasite-code.md) to send the files back via a UNIX socket using `SCM_RIGHTS`. CRIU then calls `fstat()` on these files and checks the `st_mode` field.

For certain specialized files (e.g., `signalfd`, `inotify`), the mode field may be zero. In these cases, CRIU reads the `/proc/$pid/fd/$fd/` link; the link target uniquely identifies the Inode type.

### State of File and Inode

From the File object, CRIU primarily needs the access mode and the current position. Both are available in `/proc/$pid/fdinfo/$fd/`, and the position can also be retrieved via `lseek()`.

Gathering Inode-specific state depends on the type. CRIU uses several sources:

1. Data from `/proc/$pid/fdinfo/$fd/`.
1. The target of the `/proc/$pid/fd/$fd/` link.
1. Inode-specific `ioctl()` calls.
1. Direct data retrieval (e.g., using `recv` with `MSG_PEEK` for socket queues or `tee` for pipes/FIFOs).
1. `sock_diag` modules for detailed socket information.
