# TTYs




= Overview =

Terminals are one of the most important parts of how programs interact with users. Usually programs print output into them and read user input from. For example when one executes any program from a shell, the shell provides terminal peer into it, so the program output will be seen in a calling shell and a user can do additional processing (say grepping and etc).

## Supported terminal types

There are wide range of terminals exist in linux world: *unix98* psesudoterminals (**pty**), *bsd* terminals, *virtual* terminals (**vt**) and etc. In CRIU we support the **pty** completely and **vt** in a way full enough for work. By full support we mean complete save of **pty** internal state including queued data. In turn for **vt** only plain restoration supported which means it is complete enough to do a traditional operations with terminals after restore but if there was some data queued and not yet delivered to a terminal user it will be lost. It is worth to mention that such situation should not be treated as any kind of error - the terminal transport layer never ever be one with guaranteed data delivery.

Overall CRIU supports the following terminals:

- console
- current
- virtual
- external
- serial
- unix98

### Console terminal

-*console** terminal is the most simple one. Its restore is just literally `open(/dev/console)`.

### Current terminal

-*current** terminal is rather an abstraction over real terminal an application uses. Upon its opening the kernel simply provides back the reference to the real one thus the same way as for **console** its restore is just `open(/dev/tty)` with one exception - it must be restored last, ie after all other terminals are restored.

### Virtual terminal

-*vt** stands for *ttyN* devices for which restore we simply do `open(/dev/ttyN)`, where N is a number.

### External terminal

-*external** terminals stands for cases when file descriptors known to be changing between checkpoint/restore cycles and passed from command line options, see [Inheriting FDs on restore](inheriting-fds-on-restore.md) for details. For this kind of terminals we are relying on the file descriptors to be already opened and passed, so on restore we simply reuse them.

### Serial terminal

-*serial** terminals are supported for debug purpose mostly, in particular some developers pass through into virtual machine with this terminals. Its restore as simple as plain `open()` call.

### Unix98 terminal

-*pty** terminals are most commonly used over all other kinds. The **pty** represent a pair of peers: upon `open(/dev/ptmx)` the kernel automatically create `/dev/pts/N` slave peer, where N is a numeric index of the pair opened. Thus on restore we simply need *ptmx* device and give process master and slave descriptors.

## See also
- [External files](external-files.md)