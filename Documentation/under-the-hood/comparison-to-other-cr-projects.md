# Comparison to Other C/R Projects

This page explains the differences between CRIU and other checkpoint/restore (C/R) solutions.

## DMTCP

See [DMTCP](dmtcp.md).

## BLCR

Berkeley Lab Checkpoint/Restart (BLCR) is part of the Scalable Systems Software Suite, developed by the Future Technologies Group at Lawrence Berkeley National Lab with funding from the U.S. Department of Energy. It is an open-source, system-level checkpointer designed for High Performance Computing (HPC) applications, particularly CPU- and memory-intensive batch-scheduled MPI jobs. BLCR is implemented as a GPL-licensed loadable kernel module for Linux 2.4.x and 2.6.x kernels on x86, x86_64, PPC/PPC64, and ARM architectures, along with a small LGPL-licensed library.

## PinLIT / PinPlay

PinLIT (Pin-Long Instruction Trace) is a checkpointing tool built on top of Intel's proprietary [PIN binary instrumentation tool](https://software.intel.com/en-us/articles/pin-a-dynamic-binary-instrumentation-tool), as described in [Cristiano Pereira's PhD thesis](https://cseweb.ucsd.edu/~calder/papers/thesis-cristiano.pdf). It records the processor's architectural register state and all memory pages containing application and shared library code, optimizing size by only storing memory used during a specific interval.

[PinPlay](https://software.intel.com/en-us/articles/program-recordreplay-toolkit) (the Program Record/Replay Toolkit) is the successor to PinLIT. Both tools primarily focus on reducing benchmark runtime on computer architecture simulators using sampling algorithms like SimPoint.

## OpenVZ (In-Kernel)

Legacy OpenVZ (based on RHEL4, RHEL5, and RHEL6 kernels) features in-kernel checkpoint/restore. The source code can be found in `kernel/cpt/`.

## CKPT (In-Kernel)

(In-kernel) [Linux Checkpoint/Restore](https://ckpt.wiki.kernel.org/index.php/Main_Page) was a project active from roughly 2008 to 2010 that aimed to implement checkpoint/restore for Linux processes directly within the kernel.

## CRIU, DMTCP, BLCR, and OpenVZ Comparison Table

- **"Yes/No"**: Information based on unverified reports.
- **"Not yet"**: Officially planned or no known technical barriers.

| Feature | CRIU | DMTCP | BLCR | OpenVZ |
| :--- | :--- | :--- | :--- | :--- |
| **Arch** | x86_64, ARM, AArch64, PPC64le | x86, x86_64, ARM | x86, x86_64, PPC/PPC64, ARM | x86, x86_64 |
| **OS** | Linux | Linux | Linux | Linux |
| **Uses standard kernel?** | Yes (3.11+) | Yes | Yes (requires module) | No (requires OpenVZ kernel) |
| **No preloading required?** | Yes | No | No | Yes |
| **Non-root support?** | Yes (own tasks only) | Yes | Yes | No |
| **Unmodified programs?** | Yes | Yes | No (static/threaded unsupported) | Yes |
| **Unprepared tasks?** | Yes | No (preloads library) | No (requires notification) | Yes |
| **Retains behavior?** | Yes | No (syscall wrappers) | No (syscall wrappers) | Yes |
| **Live migration** | Yes (with optimizations) | Yes | Yes (if identical environment) | Yes |
| **Containers** | Yes (LXC, OpenVZ) | No | No | Yes |
| **Parallel/Distributed libs** | Planned | Yes | Yes | Yes |
| **C/R of gdb + app?** | No | Yes | No | Yes |
| **X Window apps** | Yes (via VNC) | Yes (via VNC) | No | Yes (via VNC) |
| **Custom software API** | Yes (RPC, C API) | Yes (Plugins, API) | Not yet | Yes (via ioctl) |
| **Unix sockets** | Yes | Yes | No | Yes |
| **UDP sockets** | Yes | Not yet | Not yet | Yes |
| **TCP sockets** | Yes | Yes | Not yet | Yes |
| **Established TCP** | Yes | No (requires plugin) | No | Yes |
| **Infiniband** | No | In progress | No | No |
| **Multithread support** | Yes | Yes | Yes | Yes |
| **Multiprocess** | Yes | Yes | Yes | Yes |
| **Groups and sessions** | Yes | Yes | Not yet | Yes |
| **Zombies** | Yes | No | No | Yes |
| **Namespaces** | Yes | No | No | Yes |
| **Ptraced programs** | No | Yes | No | Yes |
| **System V IPC** | Yes | Yes | No | Yes |
| **Memory mappings** | Yes (all kinds) | Yes | Partial | Yes |
| **Pipes** | Yes | Yes | Not yet | Yes |
| **Terminals** | Yes (Unix98 PTYs) | Yes | Yes | Yes |
| **Non-POSIX files** | Yes (inotify, epoll, etc.) | Yes | No | Yes |
| **Timers** | Yes | No | Yes | Yes |
| **Shared resources** | Yes | Yes | Planned | Yes |
| **Block devices** | No | Yes | No | No |
| **Character devices** | Partial (/dev/null, etc.) | Partial | Partial | Yes |
| **Open file content** | Yes (if unlinked) | No | Not yet | Yes |

## Sources

**DMTCP:**
- http://dmtcp.sourceforge.net/
- http://dmtcp.sourceforge.net/papers/dmtcp.pdf

**BLCR:**
- https://upc-bugs.lbl.gov/blcr/doc/html/
- https://ftg.lbl.gov/assets/projects/CheckpointRestart/Pubs/blcr.pdf

## External Links

- [How does DMTCP work?](http://dmtcp.sourceforge.net/FAQ.html#Internals)
