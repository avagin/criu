# Userfaultfd

This article describes the use of `userfaultfd` for lazy restoration and lazy migration in CRIU.

## Background
The [`userfaultfd`](http://man7.org/linux/man-pages/man2/userfaultfd.2.html) mechanism allows for userspace-driven paging. While its initial implementation in Linux 4.3 was tailored for KVM/QEMU, Linux 4.11 introduced a "non-cooperative" mode that enables lazy (or post-copy) restoration in CRIU.

## Concepts

- **Lazy Restoration**: The `restore` command includes a `--lazy-pages` option. In this mode, CRIU skips injecting memory pages into the process's address space and instead registers those areas with `userfaultfd`.
- **Lazy-Pages Daemon**: A dedicated daemon manages these lazy memory areas. It receives `userfault` file descriptors from CRIU via a UNIX socket, allowing it to intercept and resolve page faults and other memory events.
- **Lazy Migration**: The `dump` command also supports the `--lazy-pages` option. When enabled, the dumper retains the memory pages and allows the `lazy-pages` daemon to request them via a TCP connection as needed.

![File:Criu-memory-wflow.png](File:Criu-memory-wflow.png)

### The Daemon
After restoration, processes have lazy VMAs registered with `userfaultfd`. The `userfaultfd` descriptor is sent to the `lazy-pages` daemon before the processes resume. The daemon then monitors for UFFD events and populates the address spaces. It can retrieve pages from local images, remote images, or directly from a remote dumper.

When a restored task accesses a missing page, a page fault occurs. The `lazy-pages` daemon receives this notification and populates the required memory. To optimize the process, the daemon also proactively copies remaining memory pages in the background when no faults are pending.

#### Local and Remote Sources
- **Local Images**: The daemon uses a local engine to read pages from image files.
- **Remote Images**: The [page server](page-server.md) is run on the remote host with the `--lazy-pages` option. The daemon connects to it using the `--page-server`, `--address`, and `--port` options.
- **Migration**: During migration, the `dump` process collects pages into pipes and starts a page server. The daemon requests missing pages from this server and injects them into the task's address space using `userfaultfd`.

## Limitations

- Currently, only `MAP_PRIVATE | MAP_ANONYMOUS` mappings are supported. While newer kernels (4.11+) support `userfaultfd` for `hugetlbfs` and shared memory, these features are not yet implemented in CRIU.
- `userfaultfd` does not support mapping a single page into multiple locations simultaneously. Consequently, COW-ed pages remain COW-ed.

## See also
- [Disk-less migration](disk-less-migration.md)
- [Lazy migration](lazy-migration.md)
