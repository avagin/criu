# Copy-on-write memory

## Problem
Private anonymous mappings are tricky. They are declared to belong to a single process only and contain *its* data, but the Linux kernel optimizes the case when task calls fork() and creates a copy of itself. In this case all private anonymous mappings are "shared" between the parent and the child, but when either of them tries to modify the memory, the respective page is duplicated and the changes occur in the modifier's copy only.

When taking a dump of a process tree, it's totally correct to copy contents of all the anonymous private mappings independently and restore them in the same way -- just mmap and put the memory in there. But with this approach we effectively do the described memory duplication and thus increase memory usage by checkpointed and restore application.

To fix this, criu in version 0.3 and above does special tricks.

## How restore works to keep COW intact
We have different ideas how to restore COW (Copy-on-write)<ref>[wikipedia:Copy-on-write](wikipediacopy-on-write.md)</ref> memory. In a moment we even thought to use KSM (Kernel Shared Memory)<ref>[wikipedia:Kernel SamePage Merging (KSM)](wikipediakernel-samepage-merging-ksm.md)</ref> for that. As result we found a good way for restoring COW memory (I guess). All VMA (Virtual Memory Area)s are restored in the same way as they were created. Here are two questions:

1. Which VMAs should be inherited?
1. How to avoid intersections with criu VMAs?

The first question is not resolved completely. Now a VMA is inherited if a parent has a VMA with the same start and end addresses. This covers 99% of cases, but it doesn't work if a VMA was moved.

The second question is more interesting. Currently criu reserves continuous space for all private VMAs, then restores all VMAs one by one in this space. Inherited VMAs are moved from a parent space. All VMAs are sorted by start addresses.

![File:cow.png](File:cow.png)

In “restorer” all criu’s VMAs are unmapped and private VMAs are space apart. The complexity of this algorithm is linear. Now it looks simple, but I spent a few hours to find it.

{{Out|“Complexity is easy; simplicity is difficult. -Georgy Shpagin”}}

{{Out|“Everything should be made as simple as possible, but not more simpler. - Albert Einstein”}}

All VMAs and their contents are restored before forking children, so here is one more item. A parent can change some pages after forking a child, so such pages should be dropped from the child's VMA. For solving this problem bitmaps are used to mark touched pages and madvise() is used to remove extra pages.

One more case is not handled now. COW memory are not restored if a process is reparented to init.

## References


{{Like}}


