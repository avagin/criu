# Fsnotify

## Hardness in dumping and restoring of fsnotify

Fsnotify are implemented quite straightforth -- we can fetch watchees by their handled from procfs output:

 pos:	0
 flags:	02000000
 inotify wd:3 ino:9e7e sdev:800013 mask:800afce ignored_mask:0 fhandle-bytes:8 fhandle-type:1 f_handle:7e9e0000640d1b6d

so that on dump we can remember a watchee file handler and open it back on restore retrieving path from file descriptor
link provided by procfs.

This all works just fine until watchees are represented as children of another watch descriptor. Consider one has a
directory **dir** and two files under it **a** and **b**:

 dir
  `- a
  `- b

and a program sets up fsnotify mark on every file entry, i.e. on **dir** itself and both files. Then imagine
a program open both **a** and **b** and then unlink them. This action generates notify events which a program
may or may not read yet (thus events queue is not empty) but a user start dumping procedure. Because kernel has
not yet any API to peek events from queue (note the *peek* here means to read events without removing them
from the queue) we either should ignore the events or refuse to dump.

Refusing dumping might be an option but due to current CRIU design it turns out that we might stuck in situation
where any attempt to dump will force CRIU to generate events itself leading to endless cycle. This is mostly because
of that named *ghost* files. The *ghost* files are the files which were removed by an application but its file
descriptor is still alive. For such scenario we generate a hardlink to the deleted file at moment of dumping which
of course generates notify events.

Almost the same situation happens on restore procedure -- *ghost* files get unlinked which cause kernel to
generate events.

So until redesign of the dumping/restore procedure for fsnotify system we have to ignore nonempty notify queues
on dump and live with the fact that we're generating own events on restore.

## Chopping the knot

Here are possible ways to resolve the situation

- when dumping files gather fsnotify and ghost file descriptors into separate lists and dump them at the very late stage; then read out notify events from fsnotify descriptors
- when restoring files collect fsnotify descriptors into a root criu task deferring theis restore until all other files (from every child process) are restored; then restore notifies and read out all generated events

both ways require significant rework of CRIU design so for a while we simply print out a warning if fsnotify queue is not empty and continue processing.

## See also
- [Irmap](irmap.md)


