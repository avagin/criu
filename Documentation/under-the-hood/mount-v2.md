# Mount-v2

CRIU Mount-v2 Algorithm

## Introduction

With the introduction of the `MOVE_MOUNT_SET_GROUP` feature in Linux v5.15 ([commit 9ffb14e](https://github.com/torvalds/linux/commit/9ffb14ef61bab83fa818736bf3e7e6b6e182e8e2)), CRIU can now restore mount sharing groups independently. We can construct mount trees using private mounts and then apply sharing groups at a later stage, rather than relying on complex inheritance during mount creation.

Restoring mount propagation using the traditional approach of inheriting groups is nearly impossible due to several factors:
- CRIU lacks information about the original order or history of mount tree creation.
- Propagation can trigger the creation of numerous unintended mounts.
- Propagation can unexpectedly change the parent of an existing mount.
- "Mount traps" can occur where propagation covers an initial mount.
- "Non-uniform" propagation requires specific mount orders and temporary "lock" mounts to recreate.
- Cross-namespace sharing requires strict ordering relative to namespace creation.

For more details, see the following presentations from the Linux Plumbers Conference:
- [CRIU mounts migration: problems and solutions](https://www.linuxplumbersconf.org/event/7/contributions/640/)
- [Mount-v2 CRIU migration engine: status update](https://linuxplumbersconf.org/event/11/contributions/923/)

Below is an example of order inversion where multiple temporary mounts are required:
![File:Mounts-inverse-order-example.gif](File:Mounts-inverse-order-example.gif)

## Mount-v2 Description

The Mount-v2 algorithm is integrated into the original engine; dumping remains unchanged. Preparatory steps—such as detecting bind mounts, external mounts, and helper mounts—have been refined to make the code more robust and reusable for Mount-v2.

### Plain Mount Points

A key difference in Mount-v2 is that mounts are initially created as "plain" mounts. In the original engine, a mount with `mnt_id=1000` at `/mount/point/path` would be mounted directly into the target tree (e.g., `<root_yard>/<mntns>/mount/point/path`). This required the parent mount to exist beforehand.

In Mount-v2, this mount is first created at a flat location like `<root_yard>/mnt-1000`. This allows the tree assembly to be handled as a separate second stage. This separation enables useful heuristics, such as creating over-mounts after the mounts they cover, or creating external mounts before their corresponding bind mounts, without causing conflicts.

To maintain compatibility with existing code that expects a tree-like structure (e.g., for restoring file content or ghost files), CRIU uses a `service_mountpoint()` helper. This helper returns traditional "tree" paths for the original engine and "plain" paths for Mount-v2.

### Resolving Sharing Groups

When Mount-v2 is enabled, CRIU takes an additional step after reading mount images to resolve sharing group information (`resolve_shared_mounts_v2`). 
1. CRIU iterates through all mounts and creates a sharing group for each unique `shared_id` + `master_id` pair.
1. Groups with a non-zero `master_id` are linked to their respective parent sharing groups to form a tree.
1. If a `master_id` has no corresponding parent group within the container, CRIU detects this as external slavery and identifies the source path (either an external mount or the root container mount).

### Actual Restore Process

When Mount-v2 is enabled, `prepare_mnt_ns()` delegates to `prepare_mnt_ns_v2()`, which follows these stages:

1. **Pre-create Namespaces**: Namespaces are initially created almost empty, containing only `tmpfs` at the root and a "root yard" for assembling the mount tree. CRIU preserves `nsfs` file descriptors to re-enter these namespaces.
1. **Populate Namespaces**: CRIU iterates through the mount tree. Using `can_mount_now_v2()`, it skips mounts that depend on others (like bind mounts whose source is not yet ready) and restarts the walk as needed.
1. **Detecting Directories**: For each new mount, CRIU determines if it is a directory or a file by performing a `stat` on its parent "plain" mount point.
1. **Create Plain Mount Points**: CRIU creates an empty file or directory to serve as the "plain" mount point.
1. **Create New Mounts**: CRIU creates the actual mount. This could be a new device, a bind of the container root, or an external bind mount. This process is simpler than the original engine because sharing group inheritance is not a concern.
1. **Advanced Bind Mounts**: `do_bind_mount_v2()` uses `open_tree()` and `move_mount()` to perform bind mounts without traversing symlinks or `autofs` points.
1. **Cross-namespace Binding**: The new mount is bind-mounted into the target namespace at the same "plain" location, ensuring it is visible and accessible for subsequent steps (like restoring UNIX sockets).
1. **Set Unbindable**: Once bind mounts are complete, mounts are marked as unbindable.
1. **Assemble Trees**: CRIU moves mounts from their "plain" locations into their final positions within the mount tree. It opens file descriptors for each mount point to allow file access later.
1. **Restore Sharing Groups**: Finally, CRIU restores sharing groups across the assembled forest using `restore_mount_sharing_options()`. It traverses the sharing group trees and applies the correct sharing settings (e.g., making a mount a slave or shared).
1. **Cleanup**: CRIU removes the temporary "service" mount points for deleted mounts.

## Links

- **Virtuozzo (Original) Version**: [Mounts-v2-Virtuozzo](mounts-v2-virtuozzo.md). (Requires specific kernel support).
- **Kernel Feature**: [`MOVE_MOUNT_SET_GROUP` commit](https://github.com/torvalds/linux/commit/9ffb14ef61bab83fa818736bf3e7e6b6e182e8e2).
- **CRIU Pull Request**: [PR #1721](https://github.com/checkpoint-restore/criu/pull/1721).
