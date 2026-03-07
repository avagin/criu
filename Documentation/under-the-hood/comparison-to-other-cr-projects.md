# Comparison to other CR projects

This pages tries to explain differences between CRIU and other C/R solutions.

## DMTCP

{{:DMTCP}}

## BLCR

Berkeley Lab Checkpoint/Restart (BLCR) is a part of the Scalable Systems Software Suite , 
developed by the Future Technologies Group at Lawrence Berkeley National Lab under SciDAC 
funding from the United States Department of Energy. It is an Open Source, system-level 
checkpointer designed with High Performance Computing (HPC) applications in mind: in particular 
CPU and memory intensive batch-scheduled MPI jobs. BLCR is implemented as a GPL-licensed 
loadable kernel module for Linux 2.4.x and 2.6.x kernels on the x86, x86_64, PPC/PPC64, ARM architectures, and a 
small LGPL-licensed library.

## PinLIT / PinPlay

PinLIT (Pin-Long Instruction Trace) is a checkpointing tool built on top of Intel's proprietary [PIN binary instrumentation tool](https://software.intel.com/en-us/articles/pin-a-dynamic-binary-instrumentation-tool) described on page 48 of [Cristiano Pereira's PhD thesis](https://cseweb.ucsd.edu/~calder/papers/thesis-cristiano.pdf). It records the processor's (big) architectural register state and all pages of memory that contain application and shared library code, optimizing size by only storing memory used during a desired interval.

[PinPlay](https://software.intel.com/en-us/articles/program-recordreplay-toolkit) or the Program Record/Replay Toolkit appears to be the successor of or new name for PinLIT. 

Both tools appear primarily focused on reducing benchmark runtime on slow computer architecture simulators, leveraging sampling algorithms such as SimPoint.

## OpenVZ (in-kernel)

Legacy OpenVZ (RHEL4, RHEL5, RHEL6 based kernels) has in-kernel checkpoint/restore, sources can be found in kernel/cpt/.

## CKPT (in-kernel)

(In-kernel) [Linux Checkpoint/Restart](https://ckpt.wiki.kernel.org/index.php/Main_Page) was a project from around 2008 to around 2010 to implement checkpoint/restart of Linux processes.

## CRIU, DMTCP, BLCR, OpenVZ comparison table
 
“looks\seems like yes/no” - i found only unproved message(s) saying “yes”/“no”

“not yet” - it is officially planned or i found no reasons, why it can’t be done.


{| class="wikitable sortable"
|-
!
! CRIU
! DMTCP
! BLCR
! OpenVZ

|-
| Arch
| x86_64, ARM, AArch64, PPC64le
| x86, x86_64, ARM
| x86, x86_64, PPC/PPC64, ARM
| x86, x86_64

|-
| OS
| Linux
| Linux
| Linux
| Linux

|-
| Uses standard kernel?
| Yes, provided it's 3.11 or later
| Yes
| Yes, just needs to load module
| No. OpenVZ kernel is required

|-
| Can be used without preloading special libraries before app start?
| Yes
| No
| No
| Yes

|-
| Can be used as non-root user?
| Yes, but user can only manipulate tasks belonging to him
| Yes
| Yes
| No

|-
| Can run unmodified programs?
| Yes
| Yes
| No. Statically linked and/or threaded apps are unsupported.
| Yes

|-
| Can run unprepared tasks?
| Yes
| No. It preloads the DMTCP library. That library runs before the routine main(). It creates a second thread. The checkpoint thread then creates a socket to the DMTCP coordinator and registers itself. The checkpoint thread also creates a signal handler.
| No. CR shall notify processes when a checkpoint is to occur (before the kernel takes a checkpoint) to allow the processes to prepare itself accordingly.
| Yes

|-
| Retains behavior of the c/r-ed programs?
| Yes (but see [What can change after C/R](what-can-change-after-cr.md))
| No, because of wrappers on system calls
| No, because of wrappers on system calls
| Yes

|-
| Live migration
| Yes, even if kernel, libs, etc are newer. Can use [memory changes tracking](memory-changes-tracking.md) to decrease freeze time
| Yes, if both kernels are recent
| Yes, but if all components are the same. Even if prelinked addresses are different, it will not restore, but it can save the whole used libs and localization files to restore program on the different machine
| Yes

|-
| Containers
| Yes, LXC and OpenVZ containers
| No. It doesn't support namespaces, so it probably can’t dump containers 
| {{No|Looks like no}}
| Yes

|-
| Parallel/distributed computations libraries
| No (planned)
| Yes. OpenMPI, MPICH2, OpenMP, Cilk are alredy supported and Infiniband is in progress
| Yes. Cray MPI, Intel MPI, LAM/MPI, MPICH-V, MPICH2, MVAPICH, Open MPI, SGI MPT
| Yes

|-
| Possible to C/R of gdb with debugged app?
| No, because they are using the same interface
| Yes
| No
| Yes

|-
| X Window apps (KDE, GNOME, etc)
| Yes, via VNC
| Yes, via VNC
| {{No|Looks like no}}
| Yes, via VNC


|-
| Solutions for invocation in the custom software
| Yes, [RPC](rpc.md) and [C API](c-api.md)
| Yes, plugins and API
| {{No|Not yet}}
| Yes, via ioctl calls

|-
| colspan="4" |

|-
| Unix sockets
| Yes
| Yes
| No
| Yes

|-
| UDP sockets
| Yes, both ipv4 and ipv6
| {{No|Not yet}}. Developers of dmtcp had no request for this
| {{No|Not yet}}
| Yes

|-
| TCP sockets
| Yes
| Yes
| {{No|Not yet}}
| Yes

|-
| Established TCP connection
| Yes
| No, but you can write a simple DMTCP plugin that tells DMTCP how you want to reconnect on restart
| No
| Yes

|-
| Infiniband
| No
| {{No|Not yet, developing is on the half-way}}
| No
| No

|-
| Multithread support
| Yes
| Yes
| Yes
| Yes

|-
| Multiprocess
| Yes
| Yes
| Yes
| Yes

|-
| Process groups and sessions
| Yes
| Yes
| {{No|Not yet}}
| Yes

|-
| Zombies
| Yes
| No
| No
| Yes

|-
| Namespaces
| Yes
| No
| No
| Yes

|-
| Ptraced programs
| No
| Yes
| No
| Yes

|-
| System V IPC
| Yes
| Yes
| No
| Yes

|-
| Memory mappings
| Yes, all kinds
| Yes
| Partial
| Yes

|-
| Pipes
| Yes
| Yes
| {{No|Not yet}}
| Yes

|-
| Terminals
| Yes, but only Unix98 PTYs
| Yes
| Yes
| Yes

|-
| Non-POSIX files (inotify, signalfd, eventfd, etc)
| Yes, inotify, fanotify, epoll, signalfd, eventfd
| Yes, epoll, eventfd, signalfd are already supported and inotify will be supported in future
| {{No|Looks like no}}
| Yes

|-
| Timers
| Yes
| No. Any counter or timer active since the beginning of a process will consider the restarted process to be a new process.
| Yes
| Yes

|-
| Shared resources (files, mm, etc.)
| Yes. SysVIPC, files, fd table and memory
| Yes. System V shared memory(shmget, etc.), mmap-based shared memory, shared sockets, pipes, file descriptors
| No, but it is planned to support shared mmap regions
| Yes

|-
| Block devices
| No
| {{Yes|Looks like yes}}
| No
| No


|-
| Character devices
| Yes, only /dev/null, /dev/zero, etc. are supported
| Yes, looks like null and zero are supported
| Yes, /dev/null and /dev/zero
| Yes

|-
| Capture the contents of open files
| Yes, if file is unlinked
| {{No|Looks like no}}
| {{No|Not yet}}
| Yes

|}

## Sources
DMTCP:
-http://dmtcp.sourceforge.net/
-http://dmtcp.sourceforge.net/papers/dmtcp.pdf
-http://www.ccs.neu.edu/home/gene/papers/ccgrid06.pdf
-http://research.cs.wisc.edu/htcondor/CondorWeek2010/condor-presentations/cooperman-dmtcp.pdf
-http://dmtcp.sourceforge.net/papers/mtcp.pdf

BLCR:
-https://upc-bugs.lbl.gov/blcr/doc/html/
-https://ftg.lbl.gov/assets/projects/CheckpointRestart/Pubs/LBNL-49659.pdf
-https://ftg.lbl.gov/assets/projects/CheckpointRestart/Pubs/blcr.pdf
-https://ftg.lbl.gov/assets/projects/CheckpointRestart/Pubs/checkpointSurvey-020724b.pdf
-https://ftg.lbl.gov/assets/projects/CheckpointRestart/Pubs/lacsi-2003.pdf
-https://ftg.lbl.gov/assets/projects/CheckpointRestart/Pubs/LBNL-60520.pdf

## External links

- [How does DMTCP work?](http://dmtcp.sourceforge.net/FAQ.html#Internals)

