# vDSO

## Overview
**vDSO** (virtual Dynamic Shared Object) is a small shared library that the kernel automatically maps into the address space of every userspace program. It provides highly optimized versions of frequently used system calls—such as `gettimeofday` and `getcpu`—that can be executed without the overhead of a full context switch. Most applications access these via **libc** rather than calling them directly.

## The Challenge
While the kernel maintains backward compatibility for vDSO functions, the internal structure and memory layout of the vDSO can change between kernel versions. This poses a significant problem for CRIU: if an application is migrated to a host running a different kernel, the vDSO image captured during the dump may be incompatible with the new kernel's internals.

## Call Proxification
To address this, CRIU uses a technique called **call proxification**. During restoration, CRIU redirects calls from the original (dumped) vDSO to the version provided by the current kernel. This process involves several steps:

1. CRIU analyzes the vDSO provided by the restoration host's kernel, parsing its symbols, sections, and entry points.
2. During the restoration of the dumped vDSO memory area, CRIU patches the function entry points with redirection instructions (e.g., a `jmp` instruction on x86) that point to the corresponding functions in the new vDSO.

This allows the application to continue using its existing vDSO entry points while actually executing the code compatible with the current kernel. If CRIU detects that the dump and restoration kernels are identical, proxification is skipped.

## Proxification Challenges
- In very rare cases, a process might be checkpointed exactly while executing the first few bytes of a vDSO function. If these bytes are the ones being patched for proxification, it could lead to inconsistent state.
- If the instruction pointer is immediately after these patched entry bytes, the function may attempt to use stale data from the `vvar` page, although this is typically handled by the kernel.
