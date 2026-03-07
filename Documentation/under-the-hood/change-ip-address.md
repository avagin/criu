# Change IP address

When doing a [live migration](live-migration.md) of a process from one host to another a common question is -- how to deal with the different IP address on the destination host. Although the correct answer would be to use containers, moving a service onto different IP address might make sense. This article describes how to do it.

> **Note:** This is not yet implemented in CRIU: [https://github.com/checkpoint-restore/criu/issues/211](https://github.com/checkpoint-restore/criu/issues/211)

## Problem

Just changing the IP address and letting the things work as they used to is not possible not due to CRIU constraints, but due to how [TCP connection](tcp-connection.md) operates according to the protocol. One cannot proceed the packet flow with one IP address changed, the client would just ignore such packets.

So when talking about migrating a server to some other place with some other IP three things are to be considered.

### Listening sockets

If your server is bound to 0.0.0.0 (INADDR_ANY) then migration would "just work" there's no IP address that would mismatch. If your server is bound to some device, then you'll have to change the binding IP address. Right now this can be done by editing the [images](images.md), in particular, all PF_INET sockets sit in files.img image and [CRIT](crit.md) can be used to modify one.

### In-flight connections

These are connect()-ed, but not yet accept()-ed. We have an option --skip-in-flight that makes criu ignore these guys.

### Established sockets

These guys are tough, as they do have some real IP address wired into their configuration. Technically it's possible to restore the socket with different IP address (by modifying the inetsk.img with [CRIT](crit.md)), but as was said -- the peer would not accept that. In the worst case the connection would get stuck till TCP timeout.

## Possible solution

So if we're OK with just breaking these connections we need to teach criu to break them. There are two things to consider while doing this.

a) Dumping sockets. Since we don't really need the connection we'd need to teach criu to skip those guys. The code dumping PF_INET sockets is in criu/sk-inet.c, the code dumping IPPROTO_TCP stuff is in criu/sk-tcp.c

b) Restoring sockets. Just leaving the hole in the place where the connected socket was is not nice, the server would get wrong error codes from syscalls and, which is worse, the hole might become busy with some other file (when server does open/socket/accept/whatever) which will break server internal logic. So at restore time we'd need to put some stub into the descriptor. I would suggest addressing this dump-time and instead of dumping the established socket into image dump the socket that looks like closed one. In this case socket restoring code would just restore the closed socket into proper place.



