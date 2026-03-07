# Kcmp Trees

## Overview

When checkpointing a group of processes, many of them may share resources. CRIU must distinguish between resources that are shared and those that are unique to a process.

To achieve this, the [`kcmp`](http://man7.org/linux/man-pages/man2/kcmp.2.html) system call was introduced to the Linux kernel. It compares a specific resource between two processes and returns a result similar to [`strcmp`](http://man7.org/linux/man-pages/man3/strcmp.3.html). This allows CRIU to efficiently track shared resources using a sorting algorithm.

### API

CRIU organizes files, filesystems, virtual memory descriptors, signal handlers, and file descriptors into separate "Kcmp trees." Currently, CRIU maintains five such trees, each declared using the `DECLARE_KCMP_TREE` helper. For example:

```c
DECLARE_KCMP_TREE(vm_tree, KCMP_VM);
```

Internally, each tree is implemented as a [red-black tree](http://en.wikipedia.org/wiki/Red%E2%80%93black_tree).

As CRIU gathers process resources, it uses the `kid_generate_gen()` helper to check if a resource already exists in the tree. If the resource is new, it is added to the tree, and the caller receives a new abstract ID for use in CRIU images. If the resource is already known, the helper returns zero, indicating it has already been handled.

This mechanism is critical for preventing duplicate entries in dump images, which would otherwise lead to significant performance issues.

### Two-Tree Strategy

To minimize the number of expensive `kcmp` calls, CRIU uses two identifiers for each object: a **gen_id** and the **ID** itself.

The **gen_id** is generated from visible attributes of an object. For a file, it might be derived from the inode number, device, and position. If two objects have different `gen_id`s, they are guaranteed to be different. However, two identical `gen_id`s do not guarantee that the objects are the same.

To handle this efficiently, objects are stored in two layers of trees. The first is a red-black tree sorted by `gen_id`. If an object is not found here, it is considered new. If a match is found, CRIU must then call `kcmp` to confirm equality. Because one `gen_id` might correspond to multiple distinct objects, a second tree is maintained under each `gen_id` leaf, sorted by the results of the `kcmp` calls.
