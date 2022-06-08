# pwn
Simple Multiplayer Chess Game for X. Supports playing over a network and is supposed to support time control. Unfortunately I am not working on this anymore.

# Building
To build simply use
```
$ meson build
$ cd build
$ ninja
```
The only required dependencies are pthreads, cairo, x11, libpulse and libpulse-simple (see meson.build).

# Running
To run a simple test game on one host simply start two terminals and use
```
$ pwn -s white -l 127.0.0.1 -p 5000
```
and
```
$ pwn -c 127.0.0.1 -p 5000
```
For anything else that may work, look at the manpage.
