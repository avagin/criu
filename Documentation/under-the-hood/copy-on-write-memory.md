# Copy-on-Write Memory

## Problem
Private anonymous mappings present unique challenges. While they are intended to belong to a single process, the Linux kernel optimizes `fork()` by sharing these mappings between the parent and child. When either process modifies the memory, the kernel duplicates the respective page so that changes only affect the modifier's copy. This is known as Copy-on-Write (COW).

When dumping a process tree, it is technically correct to copy the contents of all private anonymous mappings independently and restore them—effectively just calling `mmap()` and populating the memory. However, this approach causes memory duplication, increasing the memory footprint of the restored application compared to the original.

To address this, CRIU (version 0.3 and above) employs specialized techniques to maintain COW integrity.

## How Restoration Maintains COW Integrity
We explored several ideas for restoring COW memory, including the use of Kernel SamePage Merging (KSM). Ultimately, we developed a robust method where Virtual Memory Areas (VMAs) are restored in the same way they were originally created. This involves addressing two key questions:

1. Which VMAs should be inherited?
1. How can we avoid intersections with CRIU's own VMAs?

The first question is handled by inheriting a VMA if the parent has a VMA with identical start and end addresses. This covers the vast majority of cases, though it does not currently account for VMAs that have been moved.

To address the second question, CRIU reserves continuous space for all private VMAs and restores them one by one. Inherited VMAs are moved from the parent's address space. All VMAs are sorted by their start addresses.

![File:cow.png](File:cow.png)

In the "restorer" phase, all of CRIU's own VMAs are unmapped, and private VMAs are correctly spaced. This algorithm has linear complexity. While it may seem simple now, arriving at this design took significant effort.

> "Complexity is easy; simplicity is difficult." — Georgy Shpagin

> "Everything should be made as simple as possible, but not simpler." — Albert Einstein

Since all VMAs and their contents are restored before forking child processes, a parent might modify pages after a child is forked. To ensure correctness, these modified pages must be dropped from the child's VMA. CRIU uses bitmaps to track touched pages and employs `madvise()` to remove the redundant pages.

One case currently not handled is COW memory restoration when a process is reparented to `init`.
