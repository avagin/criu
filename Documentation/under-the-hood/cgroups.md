# CGroups

This page describes how CRIU manages control groups (cgroups).

## Overview

C/R of cgroup information involves three components:

1. The groups where tasks reside.
1. The groups that exist and are visible to tasks.
1. Mountpoints of the "cgroup" filesystem.

CRIU has supported this information since version 1.3-rc1. Here is how it works.

## CGroups tasks live in

CRIU defines a "set" of cgroups. A set is a per-controller list of paths where a task resides. If the paths to groups for two tasks differ by at least one controller, they are considered to reside in different sets.

For every set, CRIU generates an ID, which is then stored in the task's `core.tc.cg_set` image. The set in which CRIU resides during dump is also generated and saved in the inventory image. The set in which the root task resides is also special—every other set (except CRIU's own) is checked to ensure it contains only subdirectories of the respective root task's set. Otherwise, the dump fails.

On restore, each task is moved into its respective set. If a task's set coincides with CRIU's, the task is not moved and remains in whatever cgroups CRIU restore was started in.

## CGroups that are visible to tasks

In addition to cgroups containing tasks, there may be other groups where no tasks reside. To capture these, CRIU identifies the root set and saves the entire cgroup tree starting from it. This information is stored in the `cgroup.controllers` image. In the same image, CRIU saves the properties of the cgroups (i.e., values read from cgroup configuration files). Note that since CRIU starts from the root set and scans the directory tree, all paths in this section are subdirectories of the root set.

To have CRIU handle this information during dump and restore, specify the `--manage-cgroups` option.

## Dumping more cgroups than are visible

In some cases, it can be useful to dump a specific cgroup subtree, regardless of which cgroups the container's tasks are in. For example, systemd-based containers like Ubuntu 16.04 will put all of their tasks in one of `/init.scope`, `/system.slice/...`, or `/user.slice/...`. By default, CRIU's cgroup engine will not dump the root of the cgroup tree `/`. The problem is that systemd opens `/` as a directory file descriptor and changes the permissions on it, resulting in errors like:

`(00.361723)      1: Error (criu/files-reg.c:1487): File sys/fs/cgroup/systemd has bad mode 040755 (expect 040775)`

The solution is for the container engine to tell CRIU the root of the tree at which to start dumping via `--cgroup-root` on dump, so that these permissions are preserved when checkpointing the cgroup tree.

## Mountpoints of "cgroup" filesystem

If found in the list of mounts, CRIU will dump one, but only the "root" mount will work. If you have bind-mounted subgroups into a container, the CRIU dump will fail.

## Restoring into different CGroups

The option syntax is `--cgroup-root [*controller*:]/*path*`. Without this option, CRIU restores tasks and groups that reside in the subtrees starting from the root task's directories. When this option is provided, the respective `*controller*`s are restored under the given `*path*`s instead.

## CGroups restoring strategy

When restoring cgroups, CRIU may encounter existing cgroup controllers. In such cases, it relies on the user to specify the desired behavior: should it overwrite existing properties with values from the image, or should it ignore them? Or perhaps it is unacceptable to modify any existing cgroup?

To resolve this, CRIU supports named restore modes, which are specified via the `--manage-cgroups=*mode*` option. The `*mode*` argument can be one of the following:

- `none`: Do not restore cgroup properties; require the cgroup to pre-exist at the time of restoration.
- `props`: Restore cgroup properties; require the cgroup to pre-exist.
- `soft`: Restore cgroup properties only if the cgroup was created by CRIU; otherwise, do not restore properties.
- `full`: Always restore all cgroups and their properties.
- `strict`: Restore all cgroups and their properties from scratch, requiring that they do not already exist in the system.
- `ignore`: Do not manage cgroups and proceed as if they do not exist.

By default, `soft` is assigned if the `--manage-cgroups` option is passed without an argument (i.e., the same as `--manage-cgroups=soft`).

## External CGroup yard
The option syntax is `--cgroup-yard path`.

Instead of trying to mount cgroups within CRIU, provide a path to a directory containing a pre-created cgroup "yard." This is useful if you do not want to grant `CAP_SYS_ADMIN` to CRIU. For every cgroup mount, there should be exactly one directory. If there is only one controller in the mount, the directory name should simply be the name of the controller. If multiple controllers are co-mounted, the directory name should be a comma-separated list of those controllers.
