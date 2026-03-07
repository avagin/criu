# Kcmp trees

## Overview

Usually we dump not just a single process but a set of them, where every process may be sharing some resources with other processes.
Thus we need somehow to distinguish which resources are shared and which are not.

For this sake [kcmp](http://man7.org/linux/man-pages/man2/kcmp.2.html) system call has been introduced to Linux kernel.
It takes two processes and compare a resource asked, returning result similar to well known
[strcmp](http://man7.org/linux/man-pages/man3/strcmp.3.html) call. This allows CRIU to track resources with a sorting algorithm.

### API

CRIU gather files, filesystems, vitrual memory descriptors, signal handlers and file descriptors associated with a process
each into Kcmp-tree. Thus at moment we are carrying five Kcmp-trees. Each declared with `DECLARE_KCMP_TREE` helper.
For example

```
DECLARE_KCMP_TREE(vm_tree, KCMP_VM);
```

Each tree internally represented as [red-black](http://en.wikipedia.org/wiki/Red%E2%80%93black_tree) tree.

When CRIU gathers process resources it check if a resource is already sitting inside of a tree calling
`kid_generate_gen()` helper. If a resource is not in a tree - it pushed into a tree
and a caller obtains new abstract ID which may be used inside CRIU images, otherwise the helper
returns zero notifying that this kind of resource already known to CRIU and has been handled earlier.

This feature is quite important to eliminate duplication of entries inside CRIU dump images, because
two processes might share a lot of resources and dumping them multiple times would cause very serious
performance issue.

### Two trees

In order to minimize the number of `kcmp` calls we use two IDs for an object -- so called *gen_id* and the *ID* itself.

The gen_id is and ID that is created based on some visible attributes of an object. E.g. for a file it's generated out of the inode number, device and position. Having two gen_id-s different we can say that the objects differ to. E.g. file with different inodes are different. But two equal gen_id-s may refer to different files too. So to check *this* we call `kcmp`.

For faster lookup we store objects in two trees. First RB-tree is the sorted by gen_id-s tree. When we fail to find an element in a tree we assume that the object we check is the new one. When we *find* an element in the tree we need to go on and call `kcmp`. But since one gen_id leaf may refer to several elements *and* kernel reports equals/greater/less from `kcmp` we create the 2nd tree under the gen_id leaf -- the sorted by ID tree where the comparison function is the `kcmp`.

