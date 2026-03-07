# Parasite code

## Overview
Parasite code is a binary blob of code built in [PIE](http://en.wikipedia.org/wiki/Position-independent_code) format for execution inside another process address space. The main and only purpose of the parasite code is to execute CRIU service routines inside dumpee tasks address space.

## Using the parasite

All architecture independent code calling for parasite service routines is sitting in `parasite-syscall.c` file. When we need to run parasite code inside some dumpee task we:
 
1. Move task into that named seized state with `ptrace(PTRACE_SEIZE, …)` helper (thus task get stopped but does not notice that someone outside is trying to manipulate it).
1. Inject and execute mmap syscall inside dumpee address space with help of `ptrace` system call, because we need to allocate a shared memory area which will be used for parasite stack and parameters exchange between CRIU and dumpee.
1. Open local copy of shared memory space from */proc/$PID/map_files/*, where **$PID** is process identificator of a dumpee.

All these actions are gathered in `parasite_infect_seized()` helper. Once parasite is prepared and placed into dumpee address space, CRIU can call for parasite service routines.

There are two modes the parasite can operate in:

1. Trap mode
1. Daemon mode

In trap mode parasite simply executes one command and yields cpu trap instruction which CRIU intercepts. This is like one command at a time mode.

In daemon mode (as name implies) parasite behaves like a unix daemon - it opens a unix socket and start listening for commands on it. Once a command is received, it is handled and the daemon returns the result back via a socket packet. The daemon continues listening for the subsequent commands to execute. All currently known commands are assembled as `PARASITE_CMD_…` enum in `parasite.h` header.

## Parasite internal structure

Internally parasite might be represented as following blocks

![thumb|upright=1|center](File:Parasite-layout.svg)

Parasite bootstrap code written in assembly language, which is specific for every architecture (x86, arm, arm64), in turn parasite daemon is common for all architectures and written in C language. We consider only x86 architecture here but other architectures have the similar approach.

While all blocks on the image above are self descriptive the sigframe (signal frame) block requires some comments. Its main purpose is to handle `rt_sigreturn()` system call which we use to restore victim’s execution context (registers and etc). It should be prepared by caller setting up original registers values and etc the victim has at the moment of parasite injection.

### Parasite bootstrap

Parasite bootstrap lives in `parasite-head.S` file and simply adjusts own stack and literally call the daemon entry point. Right after the call there is trapping instruction placed which triggers the notification to a caller that parasite has finished its work if been running in trap mode. When parasite is running in daemon mode the notifications are a bit more complex and will be considered later.

### Parasite daemon

Parasite daemon code lives in `pie/parasite.c` file. Its entry point is `parasite_daemon()`. Upon enter it opens command socket which is used to communicate with the caller. Once socket is opened the daemon comes to sleep waiting for command to appear. 

![thumb|upright=3|center](File:Parasite-daemon.svg)

Because the whole parasite memory block is a shared memory slab data exchange between CRIU and dumpee is regular read/write operations into arguments area while commands are sent as network packets.

## Curing dumpee from parasite code

Once everything is done and we no longer need parasite it is removed from the dumpee's address space by performing the following steps:

1. CRIU starts tracing the syscalls parasite is executing with help of `ptrace`
1. CRIU sends `PARASITE_CMD_FINI` to the parasite via the control socket
1. Parasite receives the command, then closes control socket and executes `rt_sigreturn()` system call
1. CRIU intercepts exit from this syscall and unmaps parasite memory area, thus reverting the dumpee back to the state it was in before parasite injection

## See also
- [Code blobs](code-blobs.md)
- [Compel](compel.md)


