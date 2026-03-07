# Vdso

### Overview

-*vDSO** stands for [*virtual dynamic shared object*](http://man7.org/linux/man-pages/man7/vdso.7.html) and basically is a shared library exported by kernel into every userspace program. It usually exports a few frequently used functions (such as gettimeofday, getcpu and etc) and userspace applications don't call them directly but make use of **libc** instead.

### A problem

While the kernel guarantees backwards compatibility (i.e. helper functions won't suddenly disappear), the internal structure of the **vDSO** itself may vary. Userspace programs won't notice such changes but it brings a huge problem to CRIU. The **vDSO** structure is highly coupled with the kernel internals. If an application is being migrated to a new kernel release (with a brand new **vDSO**) the old **vDSO** (which is stored in the application's memory) is no longer usable.

### Calls proxification

To solve this problem CRIU does that named call proxification, where all functions from the original **vDSO** code are redirected to a new one provided by the kernel where application is restored. This done in a several steps on restore:

1. Scan runtime environment for current **vDSO** and parse its structure (size, sections, symbols, etc)
1. On restore of dumped **vDSO** memory area we patch function entry points so that they are redirected to current **vDSO** (for x86 architecture is simply a `jmp` instruction)

After that an restored application can continue using original **vDSO** helpers without problems.

In case if an application is dumped and restored on same kernel CRIU detects that **vDSO** structure has not be changed and doesn't use calls proxification.

### Unsolved proxification problems

1. If an application in very unlikely case is Checkpointed over first bytes of vdso entry *and* vdso is different on the destination migration node, those might be the bytes that are being patched during proxification. If it's on vdso just after those entry-bytes, the stale data from vvar is taken (might be just fine afterall).

