# Sockets

## Unix sockets initial support

Currently we support Unix socket of all kinds, UDP both IPv4 and IPv6, TCP in Listen and (!) [Established states](tcp-connection.md) and Netlink ones.

The cpt part uses the sock_diag engine to collect extended information about socket, then CRIU uses the files dumping engine to get access to sockets state.

The restore part of Unix sockets is the most tricky part. Listen sockets are just restored, this is simple.
Connected sockets are restored like this:

1. One end establishes a listening anon socket at the desired descriptor;
1. The other end just creates a socket at the desired descriptor;
1. All sockets, that are to be connect()-ed call connect. Unix sockets do not block connect() till the accept() time and thus we continue with...
1. ... all listening sockets call accept() and ... dup2 the new fd into the accepting end.

There's a problem with this approach -- socket names are not preserved, but looking into our OpenVZ implementation I think this is OK for existing apps.


