# Pending Signals

### Introduction
A process can block a set of signals, causing them to wait in two kernel queues: one shared between threads and another private to each thread. Signals in these queues are referred to as "pending signals." Each signal includes a `siginfo` message, and several different types of `siginfo` exist.

### Dumping Pending Signals

Dumping pending signals requires capturing the `siginfo` for each signal during a dump and then restoring it later. While this sounds simple, several challenges exist. First, the kernel traditionally does not report complete `siginfo` structures to userspace; for example, it often strips the `SI_CODE` field in signal handlers. Additionally, `siginfo` received via a `signalfd` uses a different format with fixed-size fields.

To address this, the `signalfd` interface was extended. When created with the `SFD_RAW` flag, `signalfd` returns `siginfo` in its raw format. Furthermore, the `SFD_GROUP` and `SFD_PRIVATE` flags were added to allow selecting the specific queue to dump.

Once signals are captured, they must be restored. While `rt_sigqueueinfo` is suitable for this, it traditionally cannot send `siginfo` with a positive `si_code`, as those values are reserved for the kernel. However, since a process should have the right to manage its own state, it should be able to send any `siginfo` to itself during restoration.
