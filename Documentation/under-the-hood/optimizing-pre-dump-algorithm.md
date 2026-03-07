# Optimizing pre dump algorithm

This article describes the implementation of optimized pre-dumping algorithm in CRIU. 
This project is completed under the [GSoC 2019 program](https://summerofcode.withgoogle.com/projects/#6174473131130880).

## Problems in existing Pre-dump

Previously during pre-dump, target process needs to be frozen till all the memory pages are drained into pipes. Then the target process gets unfrozen and pages collected into pipes are written into image files at the end of pre-dump. This approach has two problems. First, target process remains frozen for longer duration. Second, pipes induce memory pressure in the system. If memory utilization during pre-dump is nearly equal to system's memory, then this risks running into out-of-memory failures as the pipe pages are not reclaimable.

## Solution
The optimized implementation solves above mentioned two issues of pre-dumping. Here, the target process needs to be frozen only till memory mappings are collected. Then the process will unfreeze and continue. Draining of pages from process happens while the process is running. We use [process_vm_readv](http://man7.org/linux/man-pages/man2/process_vm_readv.2.html) syscall to drain pages from process to user-space buffer by using memory mappings collected earlier. Since draining of pages and process execution happen simultaneously, there is a possibility that the process might modify memory mappings after they have been collected, in which case process_vm_readv will encounter the old mapping. This race needs to be handled on the fly for process_vm_readv to successfully drain complete iovec.

## Design issues

The following discussion covers the possibly faulty-iov locations in an iovec, which hinders process_vm_readv from dumping the entire iovec in a single invocation.

`**NOTE:**` *For easy representation and discussion purpose, we carry out further discussion at "page granularity". `length_in_bytes` will represent page count in iov instead of byte count. Same assumption applies for the syscall's return value. Instead of returning the number of bytes read, it returns a page count.*

Consider memory layout of target process:

![File:opt_img1.png](File:opt_img1.png)

Single `iov` representation: `{starting_address, length_in_bytes}`. An iovec is array of iov-s.
For above memory mapping, generated iovec: `{A,1}{B,1}{C,4}`

This iovec remains unmodified once generated. At the same time some of the memory regions listed in iovec may get modified (unmap/change protection) by the target process while process_vm_readv is reading iovec regions.

- **Case 1:**

`A` is unmapped, `{A,1}` become faulty iov

![File:opt_img2.png](File:opt_img2.png)

process_vm_readv will return -1. Increment start pointer(2), syscall will process `{B,1}{C,4}` in one go and copy 5 pages to userbuf from iov-B and iov-C.

- **Case 2:**

`B` is unmapped, `{B,1}` become faulty iov

![File:opt_img3.png](File:opt_img3.png)

process_vm_readv will return 1, i.e. page A copied to userbuf successfully and syscall stopped, since B got unmapped. Increment the start pointer to C(2) and invoke syscall. Userbuf contains 5 pages overall from iov-A and iov-C.

- **Case 3:**

This case deals with partial unmapping of iov representing more than one pagesize region. The syscall can't process such faulty iov as whole. So we process such regions part-by-part and form new sub-iovs in aux_iov from successfully processed pages.

- **Part 3.1:**

First page of `C` is unmapped

![File:opt_img4.png](File:opt_img4.png)

process_vm_readv will return 2, i.e. pages A and B copied. We identify length of iov-C is more than 1 page, that is where this case differs from Case 2.

dummy-iov is introduced(2) as: `{C+1,3}`. dummy-iov can be directly placed at next page to failing page. This will copy
remaining 3 pages from iov-C to userbuf. Finally create modified iov entry in aux_iov. Complete aux_iov look like:

`aux_iov: {A,1}{B,1}{C+1,3}*`

- **Part 3.2:**

In between page of `C` is unmapped, let's say third page

![File:opt_img5.png](File:opt_img5.png)

process_vm_readv will return 4, i.e. pages A and B copied completely and first two pages of C are also copied.

Since, iov-C is not processed completely, we need to find `partial_read_byte` count to place out dummy-iov for remainig processing of iov-C. This function is performed by analyze_iov function.

dummy-iov will be(2): `{C+3,1}`. dummy-iov will be placed next to first failing address to process remaining iov-C.
New entries in aux_iov will look like:

`aux_iov: {A,1}{B,1}{C,2}*{C+3,1}*`

## What can't be pre-dumped
The memory regions of the target process that have `PROT_READ` protection can only be pre-dumped. The syscall process_vm_readv can't process a memory region which lacks `PROT_READ` flag.
All non-`PROT_READ` memory regions are delegated to dump stage. If some process has large number of non-`PROT_READ` pages, then this pre-dump method is not suitable as it increases load on the dump stage.

## How to invoke optimized pre-dump
`--pre-dump-mode` option is added to specify the desired algorithm to be used for pre-dump. "splice" mode executes traditional parasite based pre-dumping. The "read" mode is optimized one and uses process_vm_readv for pre-dumping. "splice" is set as the default.

## Scope for more optimization
- Processing a partially read iov could be costly, when size of partially read iov is huge and processing is done page-by-page until next mapped region is encountered.

