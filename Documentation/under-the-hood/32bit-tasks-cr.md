# 32bit tasks C/R

## Compatible applications

On x86_64 there are two types of compatible applications:
- ia32 - compiled to run on i686 target, can be executed on x86_64 with `IA32_EMULATION` config option set.
- x32 - specially compiled binaries to run on x86_64 machine with `CONFIG_X86_X32` config option set.

Both of them uses 4 byte pointers thus can address no more than 4Gb of virtual memory.<br />
But x32 uses full 64-bit register set (and thus can't be launched on i686 host natively).<br />
Both of them requires additional environment on x86_64 as Glibc, libraries, and compiler support.<br />
x32 is rarely distributed (at this moment only [Debian x32 port can be easily found](https://wiki.debian.org/X32Port)).<br />
So, CRIU will support ia32 C/R at this moment, x32 support may be quite easily added on top of ia32 as needed patches have already added in kernel with ia32 C/R support.<br />
The following text uses *compatible* and *32-bit* in the meaning of ia32 applications unless otherwise specified.

## Difference between native and compat applications

From the CPU's point of view, 32-bit compatibility mode applications differ to 64-bit application by current CS (code segment selector): if corresponding value of L-bit from flags of entry in descriptors table is set the CPU will be in 64-bit mode when this segment descriptor is being used. There are some other differences between 32 and 64-bit selectors, one can read about them [in the article "The 0x33 Segment Selector (Heavens Gate)"](https://www.malwaretech.com/2014/02/the-0x33-segment-selector-heavens-gate.html). Code selectors for both bits are defined in kernel headers as `__USER32_CS` and `__USER_CS` and corresponds to descriptors in GDT (Global Descriptors Table). One can change 64-bit mode to compatibility mode by swapping CS value (e.g., with longjump).

From the Linux kernel's point of view, applications differ by values set during exec of application such as `mmap_base` or thread info flags `TIF_ADDR32`/`TIF_IA32`/`TIF_X32`.
Both native and compat applications can do 32 or 64-bit syscalls.

## Mixed-bitness applications

That's entirely possible with current kernel ABI to create mixed-bitness applications, which may be *very* entangled.
For example, one could set *both* 32-bit and 64-bit robust futex list pointers.
Or one can create multi-threaded application where some threads are executing 32-bit code, some 64-bit code.

If we ever meet application of such mixed-bitness kind, the support may be added to CRIU quite easily, but it should be done under some compile-time config as it'll add more syscalls to usual C/R where they aren't needed.

At this moment there is no plans to add such support and it's quite unlikely that we'll find such application in real world (non-syntetic test).

## Approaches to C/R compatible applications

C/R of compatible applications can be done differently, this section describes cons/pros of each, to address decision why C/R of 32-bit tasks done *that* way and not some other.

### Restore with exec() of 32-bit dummy binary vs from 64-bit CRIU

Restore of 32-bit application can be done with some daemon that runs in 32-bit mode and communicates with CRIU binary (or 32-bit CRIU subprocess).

-*Pros**:
- no kernel patches expected (not quite true: vDSO mremap() still needed support)

-*Cons**:
- CRIU code base does not have special restore daemon to communicate with - code needs to be reworked
- 64-bit app can have 32-bit child, which could be a parent to 64-bit and so on - need to re-exec native 64-bit CRIU from 32-bit dummy (or 32-bit CRIU)
- need to send to the daemon properties of restoring processes, open fds to images, share memory with parsed ps_tree and so on... The number of IPC calls will slow down restore
- restoring becomes more complicated, and if looking forward to restoring user/pid sub-namespaces, it will be too entangled
- no optimized inheritance for task's properties those erase with exec()
- will need also another daemon for x32

### Restore with a flag to sigreturn() or arch_prctl()

The initial attempt to do 32-bit C/R, was rejected by lkml community by many reasons. It should have swapped thread info flags (such as `TIF_ADDR32`/`TIF_IA32`/`TIF_X32`), unmap native 64-bit vDSO blob from process's address space and map compatible 32-bit vDSO - all according to some bit in sigframe in `rt_sigreturn()` call or some dedicated for it `arch_prctl()` call.

-*Pros**:
- Simple from the point of CRIU: just do sigreturn with a new bit set or call arch_prctl() and do sigreturn

-*Cons**:
- If 32-bit vDSO image on restored host differ from dumped (in image), need to catch task after sigreturn and make jump trampolines separately - in case of arch_prctl() simpler ([that's why arch_prctl was in initial RFC](https://lkml.org/lkml/2016/6/1/425))
- Too many points of failure for one syscall, too complicated
- Just adding a way to swap those thread info flags from userspace would result in a new races/bugs (as e.g., TASK_SIZE macro depends on TIF_ADDR32, the mmap code may do unexpected things)

After discussion in lkml, conclusion was: separate changing personality (like thread info flags) from API to map vDSO blobs, remove TIF_IA32 flag that differs 32 from 64-bit tasks and look on syscall's nature: compat syscall, x32 syscall or native syscall.

### Seizing with two 32-bit and 64-bit parasites

-*Pros**:
- no 32-bit calls in 64-bit parasite and vice-versa
- no need in exit in parasite: ptrace code doesn't allow to set 32-bit regset to 64-bit task and the reverse, running parasite the same nature as task bereaves us from those limits

-*Cons**:
- need to have two/three (for x32 also) blobs for seizing
- macros in makefiles to build two parasites
- serialization of parasite's answers: arguments to parasite differ in size - serialize them, which added not nice-looking and less readable C macros

### Current approach

FIXME

## Needs to be done (TODO)

### Kernel patch for vsyscall page

That's emulated page, not a vma - affects only in /proc/<pid>/maps for restored process. Depends on !TIF_IA32 && !TIF_X32 - Andy got patches for disabling the emulation on per-pid basics, for now I ran tests with `vsyscall=none` boot parameter because zdtm.py checks maps before/after C/R.

### Error dump on x32-bit app dumping

At this moment we'll support only compat ia32 applications, attempt to dump x32 compat binary should result in error.

### Continue removing TIF_IA32 from uprobes & Oprofile

This flag should be gone as it's suggested by Andy & Oleg.
There is quite lot of work to make kernel work without it, but small gain:
the restored ia32 process will be traced by uprobes/oprofile and stuff like that.

-*Updated**: Done by [patches](https://lore.kernel.org/all/20201004032536.1229030-1-krisman@collabora.com/T/#u) - that merged to v5.11

## External links
- [github issue](https://github.com/checkpoint-restore/criu/issues/43)

