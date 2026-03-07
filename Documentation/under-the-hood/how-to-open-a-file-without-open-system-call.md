# How to Open a File Without an `open` System Call

CRIU sometimes encounters an inode object without a corresponding name (refer to [this article](dumping-files.md) for details on inodes). This article explains when this occurs and how CRIU handles it.

## When This Occurs

Two Linux kernel APIs, `inotify_init` and `fanotify_init`, can cause this. Both take a file path as an argument but then internalize it. They identify the inode object associated with the path, set up an event generator on it, and then discard the path. The resulting file descriptor points to the event generator, not the original path.

When CRIU encounters an `inotify` or `fanotify` (collectively referred to as `fsnotify`) file descriptor, it must identify the file the generator is monitoring. Since the original path is lost, this is generally difficult.

## Retrieving the Path

In some cases, the path can be recovered by examining how Linux manages dentries and inodes.

### Inodes and Dentries

Every file on disk is represented by an inode object, which contains an ID (inode number), access rights, owner, link count, and other metadata. Names are stored only in directories as name-to-inode mappings. When a file is accessed by name, the kernel reads these mappings and creates a "dentry" object in memory. Dentries act as a cache for both existing and non-existing files to speed up lookups.

To manage memory, the kernel may shrink the dentry cache by freeing unused dentries. A dentry is considered unused if it is not referenced by child dentries or open files.

When an `fsnotify` object is created, the kernel has the full dentry chain and the inode in memory. However, since neither the inode nor the `fsnotify` object typically maintains a reference to the dentry, the dentry chain can eventually be freed.

If the dentry cache is still alive, the path can be retrieved. However, CRIU cannot rely on this, as it must handle situations where the dentry cache has been cleared.

### Tmpfs

The `tmpfs` filesystem is an exception. Since it exists only in memory and has no other storage medium, it "pins" dentries in memory. For `tmpfs`, the filename is always available.

## Opening a File via `open_by_handle_at`

Linux provides the `open_by_handle_at` system call, originally introduced for userspace NFS servers. This call allows opening an inode using an opaque "handle." This handle is a sequence of bytes that the filesystem uses to locate and open an inode. The kernel can generate a handle for an existing inode object. Since an `fsnotify` object references an inode, CRIU can request its handle. CRIU has even patched the kernel to expose this handle in `/proc/$pid/fdinfo/$fd` for `fsnotify` descriptors.

During a dump, CRIU reads the handle from `proc` and saves it. During restoration, it calls `open_by_handle_at` to retrieve the inode. CRIU then recreates the `fsnotify` object on this inode by calling the initialization function on the `/proc/self/fd/$fd` path. The kernel resolves this path to the previously opened inode, effectively restoring the `fsnotify` state without needing the original path.

## [Irmap](irmap.md)

Not all filesystems support file handles. If handles are unavailable, CRIU must use a different approach.

CRIU leverages empirical knowledge about where programs typically place `fsnotify` watches (e.g., configuration files) and performs a filesystem tree scan to match inode numbers to paths. This engine is called **irmap** (Inode Reverse MAP). It recursively scans "known" locations and caches name-inode pairs. If a required inode was encountered during the scan, `irmap` reports the name immediately.

### Caching the Irmap

Because filesystem scans can be time-consuming, CRIU starts filling the `irmap` cache during the pre-dump operation while tasks are still running. The cache is saved as `irmap-cache.img`. During subsequent pre-dumps or the final dump, CRIU reads this cache and re-validates individual entries as needed, avoiding a full rescane.
