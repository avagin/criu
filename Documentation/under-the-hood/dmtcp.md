# DMTCP

<noinclude>
This article explains the differences between CRIU and DMTCP.
</noinclude>

DMTCP implements checkpoint/restore at the library level. This means that to checkpoint/restore an application, it must be launched with the DMTCP library dynamically linked from the start. When launched this way, the DMTCP library intercepts various library calls, builds a shadow database of information about the process's internals, and then forwards requests to `glibc` or the kernel. The gathered information is used to create an application image.

With this approach, only applications known to run successfully with the DMTCP libraries can be dumped. Furthermore, DMTCP does not provide proxies for all kernel APIs (for example, `inotify()` is currently unsupported). Another implication is the potential performance overhead caused by proxying requests.

Restoring a process set is also complex, as it often requires restoring objects with predefined IDs that the kernel may not allow userspace to set. For example, the kernel traditionally does not allow forking a process with a specific PID. To work around this, DMTCP "fools" the process by intercepting `getpid()` and providing a fake PID value. This behavior can be dangerous, as the application might see incorrect information in the `/proc` filesystem if it attempts to access files via its PID.

CRIU, by contrast, does not require any libraries to be pre-loaded. It can checkpoint and restore arbitrary applications as long as the kernel provides the necessary facilities. Since support for some CRIU features was added to the kernel relatively recently, a modern kernel version is typically required.
