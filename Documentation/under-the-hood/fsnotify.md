# Fsnotify

## Challenges in Dumping and Restoring Fsnotify

The implementation of `fsnotify` is relatively straightforward—we can identify watched items by their handles from the `procfs` output:

```
pos:	0
flags:	02000000
inotify wd:3 ino:9e7e sdev:800013 mask:800afce ignored_mask:0 fhandle-bytes:8 fhandle-type:1 f_handle:7e9e0000640d1b6d
```

During a dump, CRIU records the watched file handle and, during restoration, reopens it by retrieving the path from the file descriptor link provided by `procfs`.

This works well until watched items are descendants of another watch descriptor. Consider a directory `dir` containing two files, `a` and `b`:

```
dir
 `- a
 `- b
```

If a program sets up an `fsnotify` mark on `dir` and both files, and then opens and unlinks `a` and `b`, notify events are generated. If these events are still in the queue (i.e., the program has not read them yet) when a dump begins, a problem arises. Because the kernel currently lacks an API to "peek" at events in the queue (reading them without removing them), we must either ignore these events or refuse the dump.

Refusing the dump is sometimes necessary because of CRIU's current design, where a dump attempt might force CRIU to generate its own events, leading to an endless cycle. This is primarily due to "ghost" files—files that have been removed by the application but whose file descriptors remain open. For these, CRIU generates a hard link to the deleted file during dumping, which triggers notify events. A similar situation occurs during restoration when ghost files are unlinked, again causing the kernel to generate events.

Until the `fsnotify` dump/restore procedure is redesigned, we must ignore non-empty notify queues during a dump and accept that CRIU will generate its own events during restoration.

## Potential Solutions

Possible ways to resolve this include:

- **Late-stage processing**: When dumping, collect `fsnotify` and ghost file descriptors into separate lists and process them at the very end, reading out notify events from the `fsnotify` descriptors.
- **Deferred restoration**: During restoration, collect `fsnotify` descriptors in the root CRIU task and defer their restoration until all other files from every child process are restored. Then, restore the notifies and read out all generated events.

Both approaches would require significant changes to CRIU's architecture. For now, CRIU simply prints a warning if an `fsnotify` queue is not empty and continues processing.

## See also
- [Irmap](irmap.md)
