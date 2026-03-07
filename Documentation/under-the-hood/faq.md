# FAQ

The intention of this page is to hold answers to frequently (and no so frequently) asked questions as well as some random data spread among developers' heads ;-) Note, that a big portion of FAQ-s are about [why C/R fails](why-cr-fails.md).

- <b>Q</b>: Why CRIU dumps parts of read-only mappings that map the code section of a binary? For instance, there is a mapping at, say 0x400000, that maps the code of /usr/bin/something. After dump there will be at least a page at 0x400000 in the pagemap
- <b>A</b>: The code section may have been COWed, for instance during dynamic load of the shared libraries.


- <b>Q</b>: Why my [test](zdtm-test-suite.md) cannot do privileged operation, though I run zdtm.py as root?
- <b>A</b>: That's because zdtm.py runs all sub-tests from non-existing non-root user. If you need root prio in your sub-test add `'flags': 'suid'` into test's .desc file.


- <b>Q</b>: Is it possible to do [live migration](live-migration.md) from one server to another with changing the IP address?
- <b>A</b>: Quick answer is -- if breaking the connections is OK, then yes. In details it's described [in this article](change-ip-address.md).


- <b>Q</b>: Why does dump fail with "Cannot dump half of a stream unix connection" message in logs?
- <b>A</b>: There are configurations [when C/R fails](when-cr-fails.md). This particular error is about [external UNIX socket](external-unix-socket.md).


- <b>Q</b>: Why does restore fail with "... pid mismatch ..." error in logs?
- <b>A</b>: The PID of the process of thread CRIU tries to restore is already busy. To overcome this, try [CR in namespace](cr-in-namespace.md).


- <b>Q</b>: I've built CRIU, how can I make sure it works?
- <b>A</b>: There are two minimal things to do. First is to [check the kernel](check-the-kernel.md) and the second is run the [ZDTM test suite](zdtm-test-suite.md).

## Docker

- <b>Q</b>: Cannot restore from images onto freshly created container (E.g. [this](https://github.com/xemul/criu/issues/289) gihub issue)
- <b>A</b>: CRIU doesn't checkpoint filesystem, so once you've checkpointed the container, the images contain path to files, that sit in the *modified* root tree. So you cannot restore from images on *another* root tree, you need to get the same tree back. You need to say `docker commit` after the checkpoint and restore only on this particular image.


