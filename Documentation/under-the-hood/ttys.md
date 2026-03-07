# TTYs

## Overview
Terminals (TTYs) are a critical component of how programs interact with users, providing the primary interface for output and user input. For example, when a program is executed from a shell, the shell provides a terminal peer; the program's output is displayed in the shell, where the user can perform further processing (e.g., piping to `grep`).

## Supported Terminal Types
Linux supports a wide range of terminals, including *Unix98* pseudoterminals (**pty**), *BSD* terminals, and *virtual* terminals (**vt**). CRIU provides comprehensive support for **pty** and sufficient support for **vt**.

"Full support" for **pty** includes saving the complete internal state, including queued data. For **vt**, CRIU provides plain restoration; while this is sufficient for standard terminal operations after restoration, any data queued but not yet delivered will be lost. This is not considered an error, as the terminal transport layer does not guarantee data delivery.

CRIU supports the following terminal types:
- Console
- Current
- Virtual
- External
- Serial
- Unix98

### Console Terminal
The **console** terminal is the simplest type. Restoration is performed via a simple `open("/dev/console")`.

### Current Terminal
The **current** terminal (`/dev/tty`) is an abstraction representing the terminal currently in use by an application. When opened, the kernel provides a reference to the actual terminal device. It is restored via `open("/dev/tty")` but must be restored last, after all other terminals.

### Virtual Terminal (vt)
Virtual terminals correspond to `/dev/ttyN` devices. These are restored using `open("/dev/ttyN")`, where `N` is the terminal number.

### External Terminal
**External** terminals are used when file descriptors are expected to change between checkpoint and restoration and are passed via command-line options. For more details, see [Inheriting FDs on restore](inheriting-fds-on-restore.md). CRIU relies on these descriptors being already open and simply reuses them.

### Serial Terminal
**Serial** terminals are primarily supported for debugging purposes, such as when developers use them to access virtual machines. They are restored using a standard `open()` call.

### Unix98 Terminal (pty)
**pty** terminals are the most common type. They consist of a pair of peers: opening `/dev/ptmx` causes the kernel to automatically create a corresponding `/dev/pts/N` slave peer. During restoration, CRIU opens the `ptmx` device and provides the resulting master and slave descriptors to the process.

## See also
- [External files](external-files.md)
