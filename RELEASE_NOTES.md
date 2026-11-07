# TinyOS 1.0

First presentation release for QEMU i386.

- 320x200 desktop rendered at 640x480, with movable/resizable windows.
- Files, Notes, Terminal, TinyRepo browser, Settings, and three wallpapers.
- Flat CFS2 storage with variable-size files and two independent 64 KiB IDE drives.
- TinyBasic programs, BCE compilation, file reads, and TinyRepo fetches.
- TinyRepo/1 package server and example packages.

Known limits: partitions are not supported; both drives are fixed at 64 KiB.
TinyBasic has no loops or conditional statements. Network access is limited to
the TinyRepo/1 TCP protocol.

Build with `./build.sh` and launch with `./run-qemu.sh`. The existing `disk.bin`
is preserved. `disk2.bin` is initialized to a blank 64 KiB image only when it
does not exist.
