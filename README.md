autotun
=======

Automatic SSH Tunneler

What I want to be able to do:

run a daemon (as myself?) and have a config-file that tells it which ports
locally to map to which host/ports remotely and which ssh-gw to use to do so.

I also want to be able to specify dynamic (like -D to ssh), where I act as a
SOCKS proxy or whatever so the remote end doesn't need to be specified.

The daemon will listen on all the local ports configured, and pass first-time
connections through the auth mechanism to establish the ssh connection.

authentication done via SSH will have to be (for now) agent-based and must be
set-up prior to the first connection (how to do password? a pop-up window?
seems tricky).

I think libssh will allow multiplexing multiple "channels" on one connection so
this is like managing a bunch of "ssh -L" or "ssh -D" commands.

Building
========

This needs two libraries, one, libssh, must be installed on the system. The
other is my own libiniread, which optionally be linked against statically.
The code can be found at fubarwrangler/iniread.


