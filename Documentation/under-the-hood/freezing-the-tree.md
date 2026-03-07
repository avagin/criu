# Freezing the Tree

## Introduction

Before we can begin checkpointing processes, we must ensure they cannot change their state. This includes not only preventing them from opening new files or sockets or changing sessions but also stopping them from producing new child processes that might escape the dumping procedure. In other words, the process tree and the individual processes within it must be "immobilized" during the dump. While this sounds trivial in theory, it is challenging in practice. The checkpoint is intended to be transparent to the application, meaning the application should not perceive any change in its state transitions. While processes are traditionally stopped using `SIGSTOP`, doing so can disturb the process state.

There are two primary ways to transparently stop a process tree:

- Capturing them with `ptrace`.
- Freezing them using the [freezer cgroup](https://www.kernel.org/doc/Documentation/cgroup-v1/freezer-subsystem.txt).

## Capturing with ptrace

(Section content to be added)

## Using freezer cgroup

(Section content to be added)

## See also

- [Tree after restore](tree-after-restore.md)
- [Checkpoint/Restore](checkpointrestore.md)

## External links

- https://www.kernel.org/doc/Documentation/cgroup-v1/freezer-subsystem.txt
