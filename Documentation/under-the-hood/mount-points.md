# Mount points

This page describes what we do with mount points trees.
## Introduction
When we are thinking about restoring a mount tree, we need to remember a few things:
- shared and slave groups
- how mounts are propagated inside one group
- bind mounts (rw, ro)

The algorithm described here is not able to cover all the cases, so this solution is a temporary one.

## Dump

There is nothing interesting here. We just dump information about mounts and validate them to be sure that we are able to restore them.

## Restore

Mounts are restored for a few iterations. On each iteration we enumerate all mounts and mount everything we can. On the next iteration we mount a bit more and continue to do so  step by step. The idea is that we will be able to mount something new on each iteration. If we can't mount anything, we stop and report an error telling that we can't restore this configuration.
For example, a mount can't be mounted if its parent isn't mounted yet. Or a more interesting example, a mount can't be mounted if not all the mounts from its parent shared group are mounted.

## Known issues
CRIU doesn't support configurations where two mounts of one shared group have different set of mounts. This is not a feature, this is a bug and you are welcome to fix it.

(done and checked by non_uniform_share_propagation in zdtm)

## TODO
- Read-only bind mounts
(not sure was meant here but e.g. ghost files on readonly mounts handled and checked by ghost_on_rofs zdtm test)
- Skipping mountpoints
- Enabling FS runtime

## See also
[External bind mounts](external-bind-mounts.md)


