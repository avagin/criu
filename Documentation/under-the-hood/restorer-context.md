# Restorer context

This page describes what this context is and why we need one.

## What is it?

The restorer context is the last stage of the [restore](checkpointrestore.md) process. It differs from the regular CRIU's (process) context like the [parasite code](parasite-code.md) does -- it doesn't have any libraries, it is PIE-compiled and can only work on fixed amount of memory. In this context CRIU restores

1. memory
1. timers
1. credentials
1. threads

## Why separate context?

The reasoning for this is simple -- when CRIU comes to the state when it needs to restore process' memory, it should unmap all the old mappings and map the new ones. But since CRIU process would do this operation on itself, once the old code is unmapped CRIU will seg-fault right on the exit from the `munmap()` system call. Also this code should be get over-mmaped by the mapping is restores. So we need some code that would do this. And this other code should "sit" in two address spaces simultaneously -- in the CRIU's one and in the target one.

The switch to this context is done in several steps.

First, we collect the data needed by the restorer code and puts all it into one sequential memory area. Then, knowing the data size and the restorer code size, we find the appropriate hole in the intersection of CRIU's and target mappings. Then we mmap() this region, mremap() the data into it, put the restorer blob nearby, fix the pointers (see below) and call assember's "jump" instruction to get there.

Now what "fix the pointers" mean. When we collected data for CRIU we addressed the objects in this are using pointers valid in CRIU address space. When we will jump into restorer code pointers in there should "know" where the respective objects are. So knowing where from the restorer counts pointers and the structure of the restorer data, we alter them respectively.

## What is restored there and why

So, memory is restored here for the reasons described above. Note, that here CRIU does two things only

1. Move anonymous VMAs to proper places
1. Map new file VMAs

The anonymous memory is mmaped and filled with data earlier, in restorer it's only mremap()-ed into proper addressed. The files mapping are just mmap()-ed, as data in them sits in files :)

Timers are restore here, since CRIU processes can wait for each other for some time while restoring and not to lose timer ticks there, we delay timers arming this the last moment.

Credentials are restored here to allow CRIU perform privileged operations such as fork-with-pid or chroot().

Threads are restored here for simplicity. If we restored them before, we'd have to "park" them while we change the memory layout. Instead of doing this, we first toss the memory around, then create threads.

## See also
- [Code blobs](code-blobs.md)
- [Compel](compel.md)


