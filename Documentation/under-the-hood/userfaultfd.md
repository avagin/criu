# Userfaultfd

This article describes usage of userfaultfd for lazy restore and lazy migration in CRIU.

## Background
The [userfaultfd](http://man7.org/linux/man-pages/man2/userfaultfd.2.html) mechanism is designed to allow user-space paging. Its initial implementation merged in Linux 4.3 was designed for KVM/QEMU use-case and lacked some functionality necessary for CRIU. In Linux 4.11 the userfaultfd was extended with so-called "non-cooperative" mode, that allows, at least in theory, lazy (or post-copy) restore in CRIU.

## Concepts

- The `restore` action accepts yet another API switch: option `--lazy-pages`. In this mode, `restore` skips injection of lazy pages into the processes address space, but rather registers lazy memory areas with userfaultfd.
- The lazy pages are completely handled by dedicated `lazy-pages` daemon. The daemon receives userfault file descriptors from `restore` via UNIX socket. The userfault file descriptors allow reception of page-fault and other events and resolution of these events by the daemon.
- For the migration case, the `dump` action also accepts API switch: option `--lazy-pages`. When this option is used, the `dump` keeps the memory pages and allows the `lazy-pages` daemon to request these pages via TCP connection.

![File:Criu-memory-wflow.png](File:Criu-memory-wflow.png)

### Daemon

Tasks after restore have lazy VMAs registered with userfaultfd, the fd itself is sent before resume to `lazy-pages` daemon and closed. The daemon monitors the UFFD events and repopulates the tasks address space. The `lazy-pages` daemon can get pages either from images (both local and remote) or directly from the remote side `dump`.

When the restored task accesses a missing memory page, it causes a page fault. The `lazy-pages` daemon receives the page fault notification and resolves it by populating the faulting task memory. If there were no page faults for some time, the daemon copies the task's remaining memory pages in the background.

#### Local images

The daemon uses local page-read engine to read pages from images.

#### Remote images

- The [page server](page-server.md) is run on the remote side with `--lazy-pages` option.
- The lazy-pages daemon connects to the remote [page server](page-server.md) with `--page-server` option. The `--address` and `--port` options allow setting of IP address and port of the listening [page server](page-server.md).
- Current protocol allows the lazy-pages daemon to request several continuous pages.

#### Migration
- The `dump` collects the pages into pipes and starts the [page server](page-server.md) in a mode that allows `lazy-pages` daemon to connect to it and request the memory pages
- When the restored task accesses a missing memory page, the `lazy-pages` daemon request the page from the [page server](page-server.md) running on the dump side
- After the page is received, the `lazy-pages` daemon injects it into the task's address space using userfautlfd

## Limitations

- Currently only MAP_PRIVATE | MAP_ANONYMOUS is supported. Newer kernels (4.11+) allow userfaultfd for hugetlbfs and shared memory, yet to be implemented in CRIU.
- Userfault is known not to map one page into two places. Thus -- COW-ed pages will get COW-ed.

## See also

- [Disk-less migration](disk-less-migration.md)
- [Lazy migration](lazy-migration.md)



