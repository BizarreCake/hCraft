hCraft
======

![hCraft](https://raw.github.com/BizarreCake/hCraft/master/etc/45-small.png)

What is hCraft?
---------------

hCraft is a custom implementation of a Minecraft server, currently supprting the
47th revision of the protocol (version 1.4.2). hCraft strives to be fast,
customizable and easy to use.

Features
--------

The _currently_ implemented features are:
*  The client can connect to the server.
*  Movement and block modification are relayed between connected players.
*  Worlds can be loaded from/saved to an experimental world format (*HWv1*).
*  Players can easily switch between worlds using the /w command (Multiworld!).
*  A permissions-like rank system.
*  SQLite support.

Building
--------

To build hCraft, you will need a C++11-compatible compiler and a copy of
[SCons](http://www.scons.org/). Just change to the directory that contains
hCraft, and type `scons`. That will compile and link the source code into
an executable (can be found in the created "build" directory).

### Dependencies
*  [libevent](http://libevent.org/)
*  [sqlite3](http://www.sqlite.org/)
*  [zlib](http://www.zlib.net/)
*  [yaml-cpp](http://code.google.com/p/yaml-cpp/)

IRC
---

hCraft's IRC channel can be found in `irc.divineirc.net/6667`, `#hCraft`.

Copyright
---------

hCraft is released under GNU's general public license (GPLv3), more information
can be found [here](http://www.gnu.org/licenses/gpl.html).

