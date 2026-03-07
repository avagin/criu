# Mounts-v2-Virtuozzo

Mounts v2 CRIU algorithm

This algorithm is designed to overcome problems with sharing group restore, overmounted files, mounts with namespace tags and some more smaller problems.

(assume single userns for now)

- Mounts image read stage (read_mnt_ns_img + read_mnt_ns_img_v2)
-* Read mount_infos from images for each mount namespace to lists (collect_mnt_from_image)
-* Put mounts to trees for each mount namespace (mnt_build_tree)
-* Group mounts by superblock equality into "bind" lists (search_bindmounts)
-* Prepare sharing groups
-** Group mounts into shared group by equality of (master_id + shared_id)
-** Put shared groups in tree where parent->shared_id == child->master_id
-** If two groups has same master_id, make them siblings (even if no parent)
-* Prepare "internal yard" mount_info aside (setup_internal_yards)
-** ns mountpoint "/internal-yard-XXXXXX"
-** will require writable namespace root mount
-** needed for mount stage after forking tasks
-* Prepare nested pidns procfses 
-** Copy namespace tag across "bind" list (search_nested_pidns_proc)
-** Create helpers for descendants of nested pidns procfses in "internal yard" (handle_nested_pidns_proc)
-*** These helpers get root "/" for simplicity (deleted, file/dir)
-**** no nsfs bind support
-*** ns mountpoint "/internal-yard-XXXXXX/hlp-[mnt_id]
-* Prepare "root yard"
-** helper mount with mountpoint "/tmp/.criu.mntns.XXXXXX/"
-** Merge mount trees of all mount namespaces as subdirectories of "root yard" (merge_mount_trees)
-*** mountpoint "/tmp/.criu.mntns.XXXXXX/[nns_id]"

- Mounting, first stage (before forking processes) from init task in "service" mntns (prepare_mnt_ns_v2)
-* Actually create and mount "root yard" (populate_mnt_ns_v2 -> populate_roots_yard_v2)
-* Replace mounts for after forking tasks stage (insert_internal_yards)
-** Delete nested pidns procfses from tree
-** Insert internal yards with helpers to tree
-* Walk the merged mount tree (mnt_tree_for_each) parents before children
-** Mount all mounts "plain" (do_mount_one_v2)
-*** check mount can be mounted (can_mount_now_v2) e.g. for overlay, root, external, bind or nsfs
-*** create mountpoint "/tmp/.criu.mntns.XXXXXX/mnt-[mnt_id]" (create_plain_mountpoint)
-**** dir/file detected by stat on mountpoint
-*** Mount all mounts private and "plain"
-**** just mount a new mount (do_new_mount_v2)
-***** setup as bind source for other mounts of this super block (propagate_mount_v2)
-**** bind if superblock is already mounted or external or root (do_bind_mount_v2, do_mount_root_v2)
-***** create sources for "deleted" bind mounts and leave it for now
-**** Handle internal yard (do_internal_yard_mount_v2)
-***** mount tmpfs
-***** create mountpoints for children
-***** mount host's proc helper mount inside
-*** Exept for "plain", "private" and helpers from "internal yard" we restore each mount as it should be in the final mountns (all flags and options applied)
-** This mounting all the mounts from all final mount namespaces in a single service mount namespace allows us to do "cross-namespace" bindmounts

- Mounting, second stage ("plain" to "tree" mount) (prepare_mnt_ns_v2)
-* Walk across all mount namespaces
-** unshare(CLONE_NEWNS)
-** Walk all mounts belonging to this mntns (tree order) (assemble_tree_from_plain_mounts)
-*** mountpoint "/tmp/.criu.mntns.XXXXXX/[nns_id]/[ns_mountpoint]"
-*** Open mountpoint fd before moving mount to it and save (mp_fd)
-*** Move (MS_MOVE) mount to the tree
-*** Open mount fd (root dentry on a mount) (mnt_fd)
-** Pivot root to ""/tmp/.criu.mntns.XXXXXX/[nns_id]"
-*** leaving only mounts which should be in this mntns
-* Extract "internal yard"s from the tree and put back procfses and their ancestors (extract_internal_yards)
-* Remove sources of deleted mounts making them really "deleted" from "service" mntns (remove_sources_of_deleted_mounts)

- Forking stage: fork all processes (tree order)
-* Inits also creat pid namespaces
-* Enter mount namespace
-* Mmap files from mounted filesystem to restore COW mappings
-** We assume here that we don't have file mappings on delayed mounts else we can't handle it
-** Ghost/Link remaps may be created here
-* Fork children

- Mounting, third stage (after forking processes) (from main criu task) (__fini_restore_mntns_v2)
-* Enter CT userns (fini_restore_mntns_v2)
-* For each mount namespace
-** For each procfs of this mntns (fixup_nested_pidns_proc)
-*** Enter tagged pidns
-*** Mount procfs from it in "internal yard"
-** Walk the mount tree of each mntns and mount all yet not mounted mounts to the tree
-*** Find the mountpoint for the mount via mnt_fd of parent and mp_fds of sibling overmounts
-*** Bind the mount to it from the internal yard helper or procfs helper
-**** via /proc/self/fd/<id> on hosts proc in "internal yard"
-*** Also open mnt_fd and mp_fd for a new mount (before and after bind)
-** Umount and rmdir "internal yard"

- And finally
-* Restore sharing groups for each mount (use mnt_fd to access mounts) (restore_mount_sharing_options)
-** Walk sharing group trees (parents before children)
-*** Setup first (any) mount in a group
-**** Is slave
-***** Find any mount from parent sg or find external mount source
-***** Copy sharing from it with MS_SET_GROUP
-***** Make slave
-**** Is shared - make it also shared
-*** Setup other mounts - copy sharing from the first one

- Done

Here are links to mounts-v2 implementation in Virtuozzo criu:
- Main part: https://src.openvz.org/projects/OVZ/repos/criu/commits?until=v3.12.3.12
- Delayed proc part: https://src.openvz.org/projects/OVZ/repos/criu/commits?until=v3.12.5.13
- Kernel patch for MS_SET_GROUP: https://lore.kernel.org/lkml/1485214628-23812-1-git-send-email-avagin@openvz.org/

