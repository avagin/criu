# Mounts-v2-Virtuozzo

CRIU Mounts-v2 Algorithm (Virtuozzo version)

This algorithm is designed to resolve issues with restoring sharing groups, over-mounted files, mounts with namespace tags, and other minor issues.

### 1. Mount Image Read Stage (`read_mnt_ns_img_v2`)
- Read `mount_infos` from images for each mount namespace into lists.
- Build mount trees for each namespace.
- Group mounts by superblock equality into "bind" lists.
- **Prepare Sharing Groups**:
    - Group mounts into shared groups based on `master_id` and `shared_id` equality.
    - Organize shared groups into a tree where `parent->shared_id == child->master_id`.
    - If two groups have the same `master_id`, make them siblings.
- **Set Up "Internal Yards"**:
    - Use a writable namespace root to create `/internal-yard-XXXXXX`.
    - This is used for the mounting stage after forking tasks.
- **Prepare Nested PID Namespace procfses**:
    - Copy namespace tags across bind lists.
    - Create helpers for descendants of nested PID namespace `procfses` in the internal yard.
- **Prepare "Root Yard"**:
    - Create a helper mount at `/tmp/.criu.mntns.XXXXXX/`.
    - Merge mount trees from all namespaces as subdirectories of the root yard.

### 2. First Mounting Stage (Before forking processes)
Executed from the init task in the "service" mount namespace (`prepare_mnt_ns_v2`):
- Create and mount the "root yard".
- Replace mounts for the post-fork stage by inserting internal yards and removing nested PID namespace `procfses`.
- **Walk the Merged Mount Tree**:
    - Mount all mounts "plain" and "private".
    - Check if a mount can be created (e.g., overlay, root, external, bind).
    - Create plain mount points (detecting file vs. directory via `stat`).
    - Bind mounts to the already mounted superblock or external sources.
    - Handle internal yards by mounting `tmpfs` and creating child mount points.
- This stage allows for "cross-namespace" bind mounts by maintaining all mounts within a single service namespace.

### 3. Second Mounting Stage ("Plain" to "Tree" transition)
- For each restored mount namespace:
    - Perform `unshare(CLONE_NEWNS)`.
    - Move mounts from "plain" locations into their final tree positions.
    - Open and save file descriptors for the mount points (`mp_fd`) and the mounts themselves (`mnt_fd`).
    - Perform `pivot_root()` to the namespace's root, leaving only the intended mounts.
- Extract internal yards and restore `procfses`.
- Remove temporary sources of deleted mounts.

### 4. Forking Stage
- Fork all processes in tree order.
- Recreate PID namespaces.
- Enter the correct mount namespace.
- Map files from the mounted filesystem to restore COW mappings.
- Fork children.

### 5. Third Mounting Stage (After forking processes)
Executed from the main CRIU task (`fini_restore_mntns_v2`):
- Enter the container's user namespace.
- For each mount namespace:
    - Fix up nested PID namespace `procfses` by entering the tagged PID namespace and mounting `procfs`.
    - Walk the mount tree and bind any remaining mounts from internal yard helpers.
    - Open final `mnt_fd` and `mp_fd` descriptors.
    - Unmount and remove internal yards.

### 6. Final Stage
- **Restore Sharing Groups**:
    - Use `mnt_fd` to access mounts.
    - Walk sharing group trees (parents before their children).
    - For the first mount in a group:
        - If it is a slave, find its parent group or external source and copy sharing via `MS_SET_GROUP`.
        - If it is shared, establish the shared group.
    - For other mounts in the group, copy the sharing settings from the first mount.

---

### Links
- **Main Implementation**: [Virtuozzo CRIU commits](https://src.openvz.org/projects/OVZ/repos/criu/commits?until=v3.12.3.12)
- **Delayed Proc Support**: [Virtuozzo CRIU commits](https://src.openvz.org/projects/OVZ/repos/criu/commits?until=v3.12.5.13)
- **Kernel Patch for `MS_SET_GROUP`**: [Linux Kernel Mailing List](https://lore.kernel.org/lkml/1485214628-23812-1-git-send-email-avagin@openvz.org/)
