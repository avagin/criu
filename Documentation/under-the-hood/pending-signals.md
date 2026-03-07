# Pending signals

### Introduction
A process can block a set of signals and all signals will wait in two kernel queues. One queue is shared between threads and the other is private for a thread. Signals in a queue are called pending signals. Each signal has a siginfo, it’s a small message. Several types of siginfo-s exist.

### What do we need to dump pending signals?

We need to get the siginfo for each signal on dump, and then return it back on restore. It looks simple, doesn’t it? I thought so, before I started. The first problem is that the kernel doesn’t report complete siginfo-s in user-space. In a signal handler the kernel strips SI_CODE from siginfo. When a siginfo is received from signalfd, it has a different format with fixed sizes of fields. The interface of signalfd was extended. If a signalfd is created with the flag SFD_RAW, it returns siginfo in a raw format. We need to choose a queue, so two more flags SFD_GROUP and SFD_PRIVATE were added.
Ok, signals can be dumped. Now we can think how to restore them. rt_sigqueueinfo looks suitable for that, but it can’t send siginfo with a positive si_code, because these codes are reserved for the kernel. In the real world each person has right to do anything with himself, so I think a process should able to send any siginfo to itself.

