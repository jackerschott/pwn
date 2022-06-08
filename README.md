# pwn
Simple Multiplayer Chess Game for X. Supports playing over a network and is supposed to support time control. Unfortunately I am not working on this anymore.

# Building
To build simply use
```
meson build
cd build
ninja
```
The only required dependencies are pthreads, cairo, x11, libpulse and libpulse-simple (see meson.build).
