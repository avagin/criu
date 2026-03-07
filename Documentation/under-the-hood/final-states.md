# Final States

The "final state" refers to the status of a process tree after a CRIU dump or restore operation.

There are three possible final states:
- **Running**: Processes continue execution as usual.
- **Stopped**: Processes are halted using `SIGSTOP`.
- **Dead**: Processes are terminated using `SIGKILL`.

## Changing the Default Final States

The following command-line options can be used with `criu dump` and `criu restore` to modify the final state:

- `--leave-stopped`
- `--leave-running`

## criu dump

By default, the final state after a `criu dump` is **dead**, meaning CRIU terminates the process tree immediately after dumping it.

This default behavior is intentional. If processes were left running, they might change the filesystem (e.g., deleting a file) or networking state (e.g., closing a TCP connection). Such changes would make it impossible to restore the process tree from the dump later, as the required resources would no longer be in the state captured during the dump.

However, if a process tree does not destroy critical resources, it can be left running using the `--leave-running` option. Note that even in this case, the running processes might modify state (like file contents) in a way that is logically incompatible with a future restoration from that dump.

Leaving a process tree **stopped** is often useful for debugging CRIU. If a dump was not entirely accurate, the traces can be investigated while the processes are in a stopped state.

Note: Currently, the `--leave-stopped` and `--leave-running` options are ignored for the `predump` command, which naturally requires the process tree to remain running.

## criu restore

By default, the final state after a restore is **running**, as users typically want to resume execution immediately. The `--leave-stopped` option can be used to leave the restored process tree in a stopped state.

## Resuming from a Stopped State

If a process tree was left in a *stopped* state after a dump or restore, you can resume its execution (changing its state to *running*) using the [pstree_cont.py](https://github.com/xemul/criu-scripts/blob/master/pstree_cont.py) script. Its only argument is the PID of the root process of the tree.
