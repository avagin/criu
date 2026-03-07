# Freezing the tree

## Introduction

Before we can start checkpointing processes, we have to make sure that they will not change their state. The latter not only includes opening new files, sockets, changing session and other, but also producing new children processes which, in turn, can escape from dumping procedure. In other words, the process tree itself and processes in it must be "immobilized" while we are dumping it. While sounds trivial in theory, it is problematic in real life. The checkpoint is supposed to be transparent to the application we are dumping, thus it must not notice any change in process state transition. Traditionally, processes are stopped with the stop signal, but doing so would disturb the process state.

So there are two ways to transparently stop the process tree:

- Capturing them with ptrace
- Freezing them using [freezer cgroup](https://www.kernel.org/doc/Documentation/cgroup-v1/freezer-subsystem.txt).

## Capturing with ptrace

## Using freezer cgroup

## See also

- [Tree after restore](tree-after-restore.md)
- [Checkpoint/Restore](checkpointrestore.md)

## External links

- https://www.kernel.org/doc/Documentation/cgroup-v1/freezer-subsystem.txt

