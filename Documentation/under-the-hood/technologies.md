# Technologies

This page lists technologies and tools that were developed during the implementation of CRIU and may be useful on their own. While these tools are currently maintained within the CRIU repository, they may eventually be split into standalone projects that CRIU uses as libraries or sub-tools.

## Parasite Code Injection
This functionality was split into a sub-project called [Compel](compel.md). Compel is a utility for executing code within the address space of a foreign process. It is based on the same technology CRIU uses to retrieve process-private data during a dump. While Compel is not actively maintained and may be outdated, it remains functional.

*See also: [Code blobs](code-blobs.md)*

## Managing Protocol Buffer Objects
CRIU includes [a Python module](https://github.com/xemul/criu/blob/master/pycriu/images/pb2dict.py) for converting Google Protocol Buffers (protobuf) to Python objects and vice-versa.

Unlike other conversion projects, our `pb2dict` correctly handles optional fields that contain empty repeated fields. it also supports custom field options to mark fields that require special handling. For example, you can include [opts.proto](https://github.com/xemul/criu/blob/master/protobuf/opts.proto) in your proto-file and use `criu.*` options:

`required uint64 blk_sigset = 5 [(criu).hex = true];`

Refer to the `.proto` files in the `protobuf/` directory for more examples. We also use a unique number for all custom protobuf options to avoid collisions with other projects.

*See also: [Images](images.md)*

## Sharing of Kernel Objects
Many kernel objects can be shared between tasks. For example, if a task calls `open()` and then `fork()`, the resulting file object is shared between the parent and child. Similarly, `dup()` creates two file descriptors referencing the same shared file object. In contrast, two separate `open()` calls for the same path result in two distinct file objects.

Since the kernel lacks an API to directly reveal the mapping of tasks to shared objects, CRIU uses the `kcmp()` system call. This API compares two resources (e.g., file descriptors) and reports whether they refer to the same kernel object. CRIU's [KCMPIDS](https://github.com/xemul/criu/blob/master/kcmp-ids.c) engine leverages this API to build [kcmp trees](kcmp-trees.md) and generate unique IDs for shared objects.

*See also: [Kcmp trees](kcmp-trees.md)*
