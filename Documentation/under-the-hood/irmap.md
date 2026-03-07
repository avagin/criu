# Irmap

## The Problem

How can we retrieve a file's path given its inode number and device? This challenge arises when dumping notification objects like `inotify` or `fanotify`. These are established on an inode (initially identified by a path), after which the kernel "forgets" the original path string.

CRIU leverages empirical knowledge of where these notifications are typically placed (such as configuration files) and performs a filesystem tree scan to find the path associated with a specific inode number. This engine is called **irmap** (Inode Reverse MAP). The `irmap` cache recursively scans the filesystem starting from "known" locations and records name-inode pairs. If a required inode was encountered during the scan, `irmap` retrieves the path immediately without further filesystem access.

## Caching the Irmap

Because filesystem scans can be time-consuming, CRIU performs this process while tasks are still running. The `irmap` cache filling begins during the pre-dump operation. The resulting cache is stored in the working directory as `irmap-cache.img`. During subsequent pre-dumps or the final dump, CRIU reads this cache and re-validates individual entries as needed, avoiding a full rescane.

## Other Solutions?

Currently, no other reliable APIs exist for this purpose.

## OverlayFS

[Irmap does not currently work on OverlayFS](https://github.com/checkpoint-restore/criu/issues/136).
