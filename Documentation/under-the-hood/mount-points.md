# Mount Points

This page describes how CRIU handles mount point trees.

## Introduction
When restoring a mount tree, several factors must be considered:
- Shared and slave groups.
- How mounts propagate within a group.
- Bind mounts (both read-write and read-only).

The algorithm described here is a temporary solution and may not cover all complex edge cases.

## Dump

Dumping is straightforward. CRIU captures information about the mounts and validates them to ensure they can be successfully restored.

## Restore

Mounts are restored over several iterations. In each iteration, CRIU enumerates all mounts and restores those that are currently possible. This continues step-by-step until the tree is complete. The core idea is that each successful mount may enable others in the next iteration. If an iteration completes without any new mounts being added, CRIU stops and reports an error.

For example, a mount cannot be restored if its parent has not yet been mounted, or if certain dependencies within its parent's shared group are missing.

## Known Issues
CRIU does not currently support configurations where two mounts within the same shared group have different sets of sub-mounts. This is a known bug rather than a feature.

(Note: This has been addressed and is verified by the `non_uniform_share_propagation` test in ZDTM.)

## To-Do
- **Read-only bind mounts**: While certain cases (like ghost files on read-only mounts) are handled and verified by the `ghost_on_rofs` ZDTM test, full support may require further refinement.
- **Skipping mount points**.
- **Enabling filesystem runtime**.

## See also
[External bind mounts](external-bind-mounts.md)
