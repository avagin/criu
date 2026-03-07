# CGroups

This page describes how CRIU manages CGroups.

## Overview

When talking about C/R of CGroups info, we mean three things:

1. The groups tasks live in
1. The groups that exist and are visible by tasks
1. Mountpoints of "cgroup" file system

CRIU started supporting this info since version 1.3-rc1. Here's how it works.

## CGroups tasks live in

CRIU defines a "set" of cgroups. A set is a per-controller list of paths where a task lives. If paths to groups for two tasks differ at least for one controller, they are considered to live in different sets.

For every set CRIU generates an ID, which is then stored in the task's `core.tc.cg_set` image. The set in which CRIU lives during dump is also generated and is saved in the inventory image. The set in which the root task lives in is also special -- every other set (except CRIU's one) is checked to contain only sub-dirs of the respective root task's set. Otherwise dump fails.

On restore each task is moved into the respective set. If task's set coincide with CRIU's one task isn't moved anywhere and remains in whatever cgroups CRIU restore was started.

## CGroups that are visible by tasks

Other than CGroups collected with tasks there can be other groups in which no tasks live. To pick up those CRIU gets the root set and saves all the CGroups tree starting from it. This information is stored in the `cgroup.controllers` image. In the same place CRIU saves the properties of CGroups (i.e. values read from CGroup configuration files). Note that since CRIU starts from root set and scans the directories tree, all the paths in this section are also subdirs of the root set's.

In order to make CRIU handle this information on dump and restore one should specify the `--manage-cgroups` option.

## Dumping more cgroups than are visible

In some cases, it can be useful to dump a specific cgroup subtree, regardless of what cgroups the container's tasks are in. For example, systemd-based containers like Ubuntu 16.04 will put all of their tasks in one of `/init.scope`, `/system.slice/...`, or `/user.slice/...`. By default, then, CRIU's cgroup engine will not dump the root of the cgroup tree `/`. The problem is that systemd opens `/` as a directory FD and changes the permissions on it, resulting in errors like

`(00.361723)      1: Error (criu/files-reg.c:1487): File sys/fs/cgroup/systemd has bad mode 040755 (expect 040775)`

The solution is for the container engine to tell CRIU the root of the tree to start dumping at, via `--cgroup-root` on dump, so that these permissions are preserved when checkpointing the cgroup tree.

## Mountpoints of "cgroup" file system

If found in the list of mounts, CRIU would dump one, but only the "root" mount will work. If you bind-mounted some subgroups into container, CRIU dump would fail.

## Restoring into different CGroups

The option syntax is `--cgroup-root [*controller*:]/*path*`. Without this option, CRIU restores tasks and groups that live in the subtrees starting from the root task's dirs. When this option is given, the respective `*controller*`s are restored under the given `*path*`s instead.

## CGroups restoring strategy

When restoring cgroups CRIU may meet already existing cgroup controllers and as result it relies on user choice how to behave in such case: should it overwrite existing properties with values from the image or should ignore them? Or maybe it is unacceptable to modify any existing cgroup?

To break a tie CRIU supports that named restore modes, which should be specified as an addition to `--manage-cgroups=*mode*` option. The `*mode*` argument may be one of the following:

- `none`. Do not restore cgroup properties but require cgroup to pre-exist at the moment of restore procedure.
- `props`. Restore cgroup properties and require cgroup to pre-exist.
- `soft`. Restore cgroup properties if only cgroup has been created by *criu*, otherwise do not restore properies.
- `full`. Always restore all cgroups and their properties.
- `strict`. Restore all cgroups and their properties from the scratch, requiring them to not present in the system.
- `ignore`. Don't deal with cgroups and pretend that they don't exist.

By default, `soft` is assigned if `--manage-cgroups` option passed without an argument (i.e. the same as `--manage-cgroups=soft`).

## External CGroup yard
The option syntax is `--cgroup-yard path`.

Instead of trying to mount cgroups in CRIU, provide a path to a directory with already created cgroup yard. Useful if you don't want to grant `CAP_SYS_ADMIN` to CRIU. For every cgroup mount there should be exactly one directory. If there is only one controller in this mount, the dir's name should be just the name of the controller. If there are multiple controllers co-mounted, the directory name should be a comma-separated list of controllers.

