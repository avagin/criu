# BPF Maps

BPF maps are kernel objects that store data (used by BPF programs) in the form of key-value pairs. Applications access BPF maps using file descriptors. C/R of BPF maps involves serializing their *metadata* and *data*:

**Metadata** - This includes information such as map type, key size, value size, etc. CRIU obtains this information from the `proc` filesystem and via the `bpf` system call with the `BPF_OBJ_GET_INFO_BY_FD` argument.

**Data** - This is the map's content, i.e., the actual key-value pairs. CRIU relies on batch operations to read (`BPF_MAP_LOOKUP_BATCH`) key-value pairs from maps during the checkpoint stage and to write (`BPF_MAP_UPDATE_BATCH`) them during the restore phase.

## Support for BPF Maps

CRIU currently supports C/R of the following BPF map types:

- `BPF_MAP_TYPE_HASH`
- `BPF_MAP_TYPE_ARRAY`

## To-Do

- C/R of BTF (BPF Type Format) information
- C/R of other kinds of BPF maps

## External Links
- [BPF Documentation](https://www.kernel.org/doc/html/latest/bpf/index.html)
- [Notes on BPF](https://blogs.oracle.com/linux/notes-on-bpf-1)
- [An eBPF Overview](https://www.collabora.com/news-and-blog/blog/2019/04/05/an-ebpf-overview-part-1-introduction/)
