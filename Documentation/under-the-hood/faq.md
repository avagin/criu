# FAQ

This page provides answers to frequently (and not so frequently) asked questions, along with technical details maintained by the developers. Note that many questions regarding why C/R might fail are addressed in [Why C/R fails](why-cr-fails.md).

- **Q**: Why does CRIU dump parts of read-only mappings that contain the code section of a binary? For example, there is a mapping at `0x400000` for `/usr/bin/something`, and after the dump, there is at least one page at `0x400000` in the pagemap.
- **A**: The code section may have been modified via Copy-on-Write (COW), for instance, during the dynamic loading of shared libraries.

- **Q**: Why can't my [test](zdtm-test-suite.md) perform privileged operations, even though I am running `zdtm.py` as root?
- **A**: This is because `zdtm.py` runs sub-tests as a non-existent, non-root user. If your sub-test requires root privileges, add `'flags': 'suid'` to the test's `.desc` file.

- **Q**: Is it possible to perform [live migration](live-migration.md) between servers while changing the IP address?
- **A**: The short answer is yes, provided that breaking existing connections is acceptable. More details are available in [this article](change-ip-address.md).

- **Q**: Why does a dump fail with the message "Cannot dump half of a stream unix connection" in the logs?
- **A**: This usually occurs in configurations [where C/R fails](when-cr-fails.md). This specific error relates to [external UNIX sockets](external-unix-socket.md).

- **Q**: Why does a restore fail with a "... pid mismatch ..." error in the logs?
- **A**: The PID of a process or thread CRIU is trying to restore is already in use by another process. To resolve this, consider using [CR in namespaces](cr-in-namespace.md).

- **Q**: How can I verify that my CRIU build works correctly?
- **A**: There are two primary steps: first, [check the kernel](check-the-kernel.md) compatibility, and second, run the [ZDTM test suite](zdtm-test-suite.md).

## Docker

- **Q**: Why can't I restore from images onto a freshly created container? (See [this GitHub issue](https://github.com/checkpoint-restore/criu/issues/289))
- **A**: CRIU does not checkpoint the filesystem itself. When you checkpoint a container, the images contain paths to files residing in the *modified* root filesystem. Therefore, you cannot restore from those images onto a *different* root filesystem; you must use the original filesystem state. You should `docker commit` after the checkpoint and only restore using that specific image.
