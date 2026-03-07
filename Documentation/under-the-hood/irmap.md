# Irmap

## Problem

How having an inode number and device of a file get its path back? This problem appears when dumping various notifies objects like inotify or fanotify. Those are put on an inode (that is looked up by a path) and after it the path string is forgotten.

CRIU uses the empiric knowledge where *notify-s are typically put by programs (config files and alike) and does filesystem tree scan to find out the name by the inode number. The engine is caller *irmap* which stands for **Inode Reverse MAP**. The irmap cache recursively scans the tree starting from "known" locations and remembers all the name-inode pairs it meets. If we later try to irmap some inode which was met during the first scan, no additional FS access would occur, irmap would just report the name back.

## Caching the irmap cache

Since this FS scan can be quite long, this is recommended to be done while tasks are not frozen. So the irmap cache fill is also started on the pre-dump operation, when tasks are not frozen. After the scan the cache is stored in the working dir under the irmap-cache.img name. When CRIU's next pre-dump or final dump is performed, the irmap cache is read back and when required the cached entries are re-validated individually, w/o the full FS re-scan.

## Other solutions?

We're currently unaware of any :(

## OverlayFS

[Irmap doesn't work on this FS](https://github.com/checkpoint-restore/criu/issues/136)



