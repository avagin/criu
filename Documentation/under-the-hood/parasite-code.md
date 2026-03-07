# Parasite Code

## Overview
Parasite code is a binary blob compiled in Position-Independent Executable ([PIE](http://en.wikipedia.org/wiki/Position-independent_code)) format for execution within the address space of another process. Its primary purpose is to execute CRIU service routines within the context of the dumpee tasks.

## Using the Parasite

The architecture-independent logic for calling parasite service routines is located in `parasite-syscall.c`. To run parasite code within a dumpee task:
 
1. The task is moved into a "seized" state using `ptrace(PTRACE_SEIZE, ...)`. This stops the task without it perceiving external manipulation.
1. An `mmap` syscall is injected and executed within the dumpee's address space via `ptrace`. This allocates a shared memory area for the parasite's stack and for parameter exchange between CRIU and the dumpee.
1. CRIU opens its own local copy of this shared memory via `/proc/$PID/map_files/`.

These actions are coordinated by the `parasite_infect_seized()` helper. Once the parasite is positioned, CRIU can invoke its service routines.

The parasite operates in two modes:

1. **Trap Mode**: The parasite executes a single command and then yields via a CPU trap instruction, which CRIU intercepts. This is a one-command-at-a-time execution mode.
1. **Daemon Mode**: The parasite acts like a UNIX daemon. It opens a UNIX socket and listens for commands. Upon receiving a command, it processes it and returns the result via a socket packet. The daemon then resumes listening for subsequent commands. Supported commands are defined in the `PARASITE_CMD_...` enum in `parasite.h`.

## Internal Structure

The parasite consists of the following functional blocks:

![File:Parasite-layout.svg](File:Parasite-layout.svg)

The bootstrap code is written in architecture-specific assembly (x86, ARM, ARM64), while the parasite daemon is common across architectures and written in C. 

The **sigframe** (signal frame) block deserves special mention. Its purpose is to handle the `rt_sigreturn()` system call, which is used to restore the victim's original execution context (registers, etc.) after the parasite's work is complete. It is prepared by the caller using the register values the victim had at the moment of injection.

### Parasite Bootstrap

The bootstrap code resides in `parasite-head.S`. It adjusts its own stack and calls the daemon's entry point. Immediately following the call is a trapping instruction that notifies the caller when the parasite has finished its work (when in trap mode).

### Parasite Daemon

The daemon code is located in `pie/parasite.c`, with `parasite_daemon()` as its entry point. Upon starting, it opens a command socket to communicate with CRIU. The daemon then waits for commands.

![File:Parasite-daemon.svg](File:Parasite-daemon.svg)

Since the parasite memory block is a shared memory slab, data exchange between CRIU and the dumpee is performed via standard read/write operations in the arguments area, while commands are transmitted as network packets.

## Removing Parasite Code from the Dumpee

Once the parasite is no longer needed, it is removed using these steps:

1. CRIU begins tracing the syscalls executed by the parasite using `ptrace`.
1. CRIU sends the `PARASITE_CMD_FINI` command via the control socket.
1. The parasite closes the socket and executes an `rt_sigreturn()` system call.
1. CRIU intercepts the completion of this syscall and unmaps the parasite's memory area, returning the dumpee to its original state.

## See also
- [Code blobs](code-blobs.md)
- [Compel](compel.md)
