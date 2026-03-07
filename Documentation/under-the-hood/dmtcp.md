# DMTCP

<!--

WARNING! ACHTUNG! UWAGA! ВНИМАНИЕ! POZOR!

When editing this article, remember its contents is also being included into [Comparison to other CR projects](comparison-to-other-cr-projects.md).
Make sure that one looks OK after you have edited this.

If you add something here that should not be included there, wrap it into <noinclude>. 
Pay attention to formatting issues such as amount of vertical whitespace. Thanks!

-->
<noinclude>This article tries to explain the differences between CRIU and DMTCP.
</noinclude>
DMTCP implements checkpoint/restore of a process on a library level. This means, that if you want to C/R some application you should launch one with DMTCP library (dynamically) linked from the very beginning. When launched like this, the DMTCP library intercepts a certain amount of library calls from the application, builds a shadow data-base of information about process' internals and then forwards the request down to glibc/kernel. The information gathered is to be used to create an image of the application. With this approach, one can only dump applications known to run successfully with the DMTCP libraries, but the latter doesn't provide proxies for all kernel APIs (for example, inotify() is known to be unsupported). Another implication of this approach is potential performance issues that arise due to proxying of requests.

Restoration of process set is also tricky, as it frequently requires restoring an object with the predefined ID and kernel is known to provide no APIs for several of them. For example, kernel cannot fork a process with the desired PID. To address that, DMTCP fools a process by intercepting the getpid() library call and providing fake PID value to the application. Such behavior is very dangerous, as application might see wrong files in the /proc filesystem if it will try to access one via its PID.

CRIU, on the other hand, doesn't require any libraries to be pre-loaded. It will checkpoint and restore any arbitrary application, as long as kernel provides all needed facilities. Kernel support for some of CRIU features were added recently, essentially meaning that a recent kernel version might be required.

