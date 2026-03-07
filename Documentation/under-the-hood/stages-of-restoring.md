# Stages of Restoration

The restoration process involves several global synchronization points:

| State | Description |
| :--- | :--- |
| `CR_STATE_FORKING` | Create tasks and restore process group leaders. |
| `CR_STATE_RESTORE_PGID` | Restore process group IDs. Group leaders and all helpers must exist at this point. |
| `CR_STATE_RESTORE` | Wait for helpers and restore the majority of resources. If a process segfaults, its parent receives `SIGCHLD` and notifies CRIU. |
| `CR_STATE_RESTORE_SIGCHLD` | Restore `SIGCHLD` handlers. |
| `CR_STATE_COMPLETE` | All handlers are restored, and the original processes can be resumed. |
