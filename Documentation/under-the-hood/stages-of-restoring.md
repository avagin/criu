# Stages of restoring

A process of restoring has a few global synchronization points:
{| class="wikitable"
| `CR_STATE_FORKING` || Create tasks and restore group leaders
|-
| `CR_STATE_RESTORE_PGID` || Restore group IDs. Group leaders should exist in this moment and all helpers should be alive
|-
| `CR_STATE_RESTORE` || Wait helpers and restore a most part of resources. If anyone segfauls, its parent gets `SIGCHLD` and notify criu about that
|-
| `CR_STATE_RESTORE_SIGCHLD` || Restore `SIGCHLD` handlers
|-
| `CR_STATE_COMPLETE` || All handlers were restored, origin processes can be resumed.
|}


