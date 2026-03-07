# Optimizing the Pre-dump Algorithm

This article describes the implementation of an optimized pre-dumping algorithm in CRIU. This project was completed as part of the [GSoC 2019 program](https://summerofcode.withgoogle.com/projects/#6174473131130880).

## Problems with the Existing Pre-dump

Previously, during a pre-dump, the target process had to remain frozen until all memory pages were drained into pipes. These pages were only written to image files at the end of the pre-dump. This approach had two major drawbacks:
1. The target process remained frozen for a significant duration.
2. The pipes created significant memory pressure. If memory usage during the pre-dump approached the system's total capacity, it risked triggering out-of-memory (OOM) failures, as pipe pages are non-reclaimable.

## The Solution

 The optimized implementation addresses these two issues. Now, the target process is only frozen until its memory mappings are collected. Once collected, the process is unfrozen and resumes execution. The draining of pages from the process occurs while the process is running, using the [`process_vm_readv`](http://man7.org/linux/man-pages/man2/process_vm_readv.2.html) system call to copy data into a userspace buffer. 

Because page draining and process execution happen simultaneously, the process might modify its memory mappings (e.g., unmapping a region) after CRIU has collected them. This race condition must be handled on the fly to allow `process_vm_readv` to complete the transfer.

## Design Considerations

The following sections discuss how to handle "faulty" locations within an `iovec` that prevent `process_vm_readv` from processing the entire vector in a single call.

**Note**: For simplicity, the following discussion uses "page granularity." `length_in_bytes` represents the page count, and the syscall's return value reflects the number of pages successfully read.

Consider the memory layout of a target process:

![File:opt_img1.png](File:opt_img1.png)

A single `iov` is represented as `{starting_address, page_count}`. For the layout above, the `iovec` would be: `{A, 1}, {B, 1}, {C, 4}`.

While this `iovec` is static once generated, the target process may unmap or change the protection of these regions while `process_vm_readv` is active.

### Case 1: The first region is unmapped
If region `A` is unmapped, `{A, 1}` becomes a faulty `iov`.

![File:opt_img2.png](File:opt_img2.png)

`process_vm_readv` will return `-1`. By incrementing the start pointer, the next call will process `{B, 1}, {C, 4}` and successfully copy 5 pages.

### Case 2: A middle region is unmapped
If region `B` is unmapped, `{B, 1}` becomes faulty.

![File:opt_img3.png](File:opt_img3.png)

`process_vm_readv` will return `1`, indicating page `A` was successfully copied before the syscall encountered the unmapped region `B`. CRIU then increments the pointer past `B` and resumes with region `C`.

### Case 3: Partial unmapping of a large region
If a large region (e.g., `C`) is partially unmapped, `process_vm_readv` cannot process the faulty `iov` as a whole. CRIU must process these regions part-by-part.

**Part 3.1: The first page of a region is unmapped**
![File:opt_img4.png](File:opt_img4.png)
`process_vm_readv` returns `2` (pages `A` and `B` copied). CRIU identifies that `iov-C` is larger than one page and introduces a "dummy-iov" `{C+1, 3}` to attempt to copy the remaining pages of the region.

**Part 3.2: A page in the middle of a region is unmapped**
![File:opt_img5.png](File:opt_img5.png)
`process_vm_readv` returns `4` (A, B, and the first two pages of C copied). CRIU calculates the `partial_read_byte` count and creates a dummy-iov `{C+3, 1}` to skip the faulty page and copy the remainder of region `C`.

## Limitations

Only memory regions with `PROT_READ` protection can be pre-dumped. `process_vm_readv` cannot access regions lacking this flag. Non-readable regions are deferred to the final dump stage. If a process has a large number of such pages, the benefits of this optimized pre-dump are reduced.

## Invocation

The `--pre-dump-mode` option allows users to select the algorithm. 
- `splice`: Traditional parasite-based pre-dumping (default).
- `read`: Optimized pre-dumping using `process_vm_readv`.

## Future Optimization

Processing partially read `iov`s can become expensive if the region is very large and CRIU must iterate page-by-page to find the next valid mapping.
