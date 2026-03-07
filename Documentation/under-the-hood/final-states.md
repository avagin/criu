# Final states

Final state is the state a process tree ends up in after CRIU dump or restore.

There are 3 possible final states:
**Running : processes are running as usual**
**Stopped : processes are stopped using SIGSTOP**
**Dead    : processes are destroyed using SIGKILL**

## Changing the default final states

You can use the following command line options with CRIU dump and restore commands
to change the default process tree final state:

- `--leave-stopped`
- `--leave-running`

## criu dump

By default, the final state of a process tree after `criu dump` is *dead*,
meaning CRIU kills the process tree right after dumping it.

Such a default makes lot of sense. Suppose the process tree is left running after being dumped.
If processes are left running, they will most probably change the filesystem state (for example,
delete a file) and/or networking state (for example, close a TCP connection). After such changes
CRIU won't be able to restore the process tree from the dump, as the system simply won't have
resources that this process tree requires at the moment when it was captured.

On the contrary, if a process tree does not destroy any resources it depended upon before dump,
then it is possible to restore it after dump made with `--leave-running` option.
Be aware, though, that the process tree left running after the dump may modify some state
(for example, write to a file) in way that is not compatible on the application level with
the process tree restored from this dump later.

Now, let's consider leaving the process tree stopped after performing the dump.
One possible use case for that is CRIU debugging. If CRIU was not quite accurate doing dump,
then it would leave some traces in the dumped process tree. You can investigate such a process tree
in its stopped state. Surely you might find some other cases to use `--leave-stopped`.

Also, leaving the process tree running is naturally necessary for the `predump`
command, so currently `--leave-stopped` and `--leave-running` options
are ignored for `predump`.

## criu restore

By default, the final state of a process tree after restore is *running*. That's because one usually
wants to immediately resume execution of the process tree after restore. One can use `--leave-stopped`
option to restore the process tree and leave it in ''stopped' state.

## Changing the state from stopped to running

After criu dumped or restored a process tree and left it in *stopped* state,
we may need to continue its execution, changing the state to *running*.
To do that, use the [pstree_cont.py](https://github.com/xemul/criu-scripts/blob/master/pstree_cont.py)
script. Its sole argument is the PID of a process tree root process.


