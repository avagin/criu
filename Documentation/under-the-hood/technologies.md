# Technologies

This page contains a list of technologies and tools have been developed while we were implementing CRIU, which might be useful on their own. These tools are still maintained within the CRIU itself, but eventually they can be split out into a standalone project and CRIU will be fixed to use those as libraries/sub-tools.

## Parasite code injection
This thing was split into a sub-project called [compel](compel.md).
Compel is an utility to execute code in foreign process address space. It is based on the same technology we use in CRIU to obtain some process-private data on dump.
We don't currently maintain compel and it's quite out-dated, but still functional.

-See also: [Code blobs](code-blobs.md)*

## Managing protocol buffers objects
We have [a Python module](https://github.com/xemul/criu/blob/master/pycriu/images/pb2dict.py) to convert google protocol buffers to python objects and back.

Unlike other projects to convert pb to python dict or json, our pb2dict does treat optional fields with empty repeated inside properly. It includes special support for custom field options to mark those needed to be treated in a special way.
For example, you could include our [opts.proto](https://github.com/xemul/criu/blob/master/protobuf/opts.proto) in your proto-file and use criu.* options
to mark your fields, i.e.:

`required uint64 blk_sigset = 5[(criu).hex = true];`

See more examples in our protobuf/*.proto files.

It is also worth noting, that we have a unique number for all kinds of custom protobuf options, so you don't have to worry that it might collide with others.

-See also: [Images](images.md)*

## Sharing of kernel objects

It's well known that some kernel objects can be shared between tasks. For example, if a task calls `open()` to obtain a file descriptor and then `fork()`-s the file object referenced by the file descriptor becomes shared between the parent and its child. The similar thing happens when a task `dup()`-s a file -- he gets two file descriptors that reference the same file. Unlike this two subsequent `open()`-s result in two different file objects.

In kernel there's no API that can just reveal the tasks-to-objects map. Instead, there's an API called `kcmp()` that compares two e.g. file descriptors and reports back whether the file referenced by them is the same or not. In CRIU we have a [KCMPIDS](https://github.com/xemul/criu/blob/master/kcmp-ids.c) engine that uses this API, build [kcmp trees](kcmp-trees.md) and turns the equals-notequals answers into a list of IDs of objects.

-See also: [Kcmp trees](kcmp-trees.md)*


