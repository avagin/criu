# Mount-v2

Mount-v2 CRIU algorithm

## Introduction

After we've merged MOVE_MOUNT_SET_GROUP feature to mainstream linux v5.15 [torvalds/linux@9ffb14e](https://github.com/torvalds/linux/commit/9ffb14ef61bab83fa818736bf3e7e6b6e182e8e2) now we can use it to restore sharing groups of mounts without the need to care about inheriting those groups when create mounts, we can just set sharing groups at later stage and before that construct mount trees with private mounts.

Restoring propagation right with conservative approach of both creating mounts and inheriting propagation groups looks like mission impossible task for us due to many problems:

- Criu knows nothing about the initial history or order of mount tree creation;
- Propagation can create tons of mounts;
- Propagation may change parent mounts for existing mount tree;
- "Mount trap" - propagation may cover initial mount;
- "Non-uniform" propagation - there are different tricks with mount order and temporary children-"lock" mounts, which create mount trees which can't be restored without those tricks;
- "Cross-namespace" sharing groups creation need to be ordered with mount namespace creation right;
- Sharing groups vs mount tree order inversion can be very complex to restore and require multiple auxiliary. (see example below)

See my talks about it on Linux Plumbers Conference:
- [CRIU mounts migration: problems and solutions](https://www.linuxplumbersconf.org/event/7/contributions/640/)
- [Mount-v2 CRIU migration engine: status update](https://linuxplumbersconf.org/event/11/contributions/923/)

And here is the example of order inversion where multiple temporary mounts needed to achieve the result:
![none|link=|Mounts-inverse-order-example.gif](File:Mounts-inverse-order-example.gif)

## Mount-v2 description

New mount-v2 algorithm is integrated deeply in the original one, so that dumping of mounts is done exactly the same for original mount engine and new one. So mount-v2 series has preparatory steps related to bindmount detection, external mounts detection and helper mounts handling to make the original mount code more robust, to make it easier to reuse it in mount-v2.

#### Plain mountpoints

One of main differences of mount-v2 comparing to original is that mounts are initially created "plain", for instance if we had **MOUNT** with **mnt_id=1000** and **ns_mountpoint="/mount/point/path"**, original mount engine would originally mount this **MOUNT** in the mount tree to **<criu_root_yard>/<mntns>/mount/point/path** so that if this mount had **PARENT** mount with **mnt_id=999** and **ns_mountpoint="/mount/point"** corresponding mount for **PARENT** would be created in **<criu_root_yard>/<mntns>/mount/point** thus restoring parent-child relationship between them initially. For mount-v2 **MOUNT** would be first mounted to **<criu_root_yard>/mnt-1000** and **PARENT** would be mounted to **<criu_root_yard>/mnt-999** so that on the first stage we only create mounts and then on separate second stage handle the tree assembling separately. This way we can have useful heuristics like on the second stage we can create overmounts after mounts they overmount, and on the  first stage we can create external mounts before their bindmounts and these two do not clinch with each other.

But it is not so simple actually because we do not want to rewrite all the code for instance for restoring mount content or restoring ghost and remap files, which used mountpoint paths in "tree" format. So in all places where it does not matter (where we do not access <criu_root_yard>/<mntns>/... paths) we switched from using mount_info->mountpoint to mount_info->ns_mountpoint and in all places where we actually needed "tree" format paths we replace them with service_mountpoint() helper which would return "tree" paths for original mount engine and "plain" paths for mount-v2. This way we can safely switch from one to another.

#### Resolving sharing groups

Just after reading mounts from images in read_mnt_ns_img() when mount-v2 is enabled we have an additional step to collect sharing group information from mounts and turn it to sharing groups forest graph (resolve_shared_mounts_v2). First, we just walk over all mounts and create sharing group for each mount with unique shared_id + master_id pair, also we sew all mounts to corresponding sharing group with same id pair. Second, we walk over all sharing groups which has non-zero master_id and lookup the corresponding parent sharing groups and connect them with a tree.

There is also a case when master_id is non-zero but there is no corresponding parent sharing group, this means that outside of dumped container there is mount with matching shared_id - external slavery detected. For this case we just collect sibling sharing groups in list with empty parent link. Also we detect source path from which the master_id would be inherited either from some mountpoint-external mount or from root container mount.

#### Actual restore of mounts

Actual restore of mounts in original mount engine starts with prepare_mnt_ns() function, when mount-v2 is enabled we pass controll from it to prepare_mnt_ns_v2() instead. It consists of several stages:

1) We pre-create mount namespaces for each restored mount namespace in pre_create_mount_namespaces(). These namespaces appear almost empty: they contain tmpfs as their root, they have root yard path created in it with another tmpfs mounted in it, and"namespace" path for assembling tree of mounts in it created in corresponding subdirectory of root yard mount. Surely we also save nsfs fds to each mount namespace to be able to reenter them later.

2) In populate_mnt_ns_v2() we reuse mnt_tree_for_each() walk over mount tree from original mount engine and so we walk mounts in tree order with addition of temporary skipping mounts and their descendants with can_mount_now_v2() in case they depend from other mounts, restarting the walk for them later. The can_mount_now_v2() is basically skipping mounts which should be restored as bindmounts but their source is not ready yet, this is true for bindmounts of root, external or plugin mounts or non-fsroot mounts.

3) In the mentioned walk over mounts forest in do_mount_one_v2() we determine if the newly created mount is directory one or a file one in detect_is_dir(), we just open its mountpoint path relative to parent "plain" mountpoint and do stat. That's why it is important to use mnt_tree_for_each() as it insures that parent is already "plain" mounted.

4) In the mentioned walk over mounts forest in do_mount_one_v2() we create "plain" mountpoint for a new mount, empty file or directory based on the previous step.

5) In the mentioned walk over mounts forest in do_mount_one_v2() we actually create new mount, either we create completely new mount or device-external in do_new_mount_v2() if it's supported, or bind container root mount in do_mount_root_v2() from the still visible host mount tree, or bind mountpoint-external mount in do_bind_mount_v2() and similarly bind any mount for which superblock is already created by other mount beforehand and we can just bind it in do_bind_mount_v2(). These functions act similar to ones in original mount engine but simplified as they don't need to care about inheriting sharing groups.

6) The do_bind_mount_v2() is improved to do bindmount via open_tree() + move_mount() with flags allowing not to traverse symlinks or autofs mounts.

7) Also we cross-namespace bindmount the newly created mount to restored mount namespace to the same "plain" mountpoint in do_mount_in_right_mntns(). So that we initially have a mount which would be visible after restore, this would be required in future to be able to restore bindmounted unix sockets on the right mount.

8) Now after the walk we don't plan to do bindmounts anymore so we set unbindable flags on mounts.

9) Next we assemble mount trees in each restored mount namespace in assemble_mount_namespaces() by again reusing move_mount_to_tree() to have tree order of moving mounts into proper places in mount tree. Also we open fds on the mountpoint: one mp_fd_id before moving and another mnt_fd_id after, so that we can access files on each mount later from final mntns via those fds.

10) Finally we do restore sharing groups on the assembled mount forest in restore_mount_sharing_options(). It walks each root sharing group and their descendants with dfs tree walk. It creates sharing for the first mount in the sharing group and then sets the same sharing on all other mounts in this group.

Sharing creation for first mount is two step:

a) If mount has master_id we either copy shared_id from parent sharing group or from external source and then make mount slave thus converting it to right master_id.
b) Next if mount has shared_id we just make us shared, creating right shared_id.

We need to use userns_call() for MOVE_MOUNT_SET_GROUP to have all right permissions for copying sharing (move_mount_set_group()). Also we need to resolve external paths given by user to their actual mountpoint, we do so with openat2(RESOLVE_NO_XDEV) in resolve_mountpoint, this also only works from userns_call().

11) We remove sources of deleted mounts making them actually deleted (from "service" mount namespace), as moving deleted mounts is not allowed and just to simplify things we do it at the last step.

#### Links

- "Virtuozzo" (original) version (using non-mainstream kernel interface): [Mounts-v2-Virtuozzo](mounts-v2-virtuozzo.md) It actually has cool features we don't have in mainstream yet, for instance - nested pidns proc handling, this feature requires nested pidns support beforehand.

- MOVE_MOUNT_SET_GROUP kernel feature: [torvalds/linux@9ffb14e](https://github.com/torvalds/linux/commit/9ffb14ef61bab83fa818736bf3e7e6b6e182e8e2)

- Mount-v2 PR to criu: [#1721](https://github.com/checkpoint-restore/criu/pull/1721)

