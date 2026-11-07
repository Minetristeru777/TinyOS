# TinyOS 1.0 for QEMU i386

A small BIOS-bootable learning OS for QEMU i386. NASM handles the boot sector
and protected-mode setup; the desktop, keyboard input, graphics, and file
manager are written in freestanding C. In QEMU it uses VBE 640x480x16 with a
compact 320x200 logical desktop scaled to fit, falling back to VGA mode 13h if
VBE is unavailable. An off-screen frame buffer reduces redraw flicker.

## Build and run

```sh
./build.sh
./run-qemu.sh
```

The system uses two independent 64 KiB CFS2 disk images: `disk.bin` (drive 0)
and `disk2.bin` (drive 1). The build creates a blank second image only when it
does not exist. Open Terminal and enter `DISK 0` or `DISK 1` to switch drives;
the Files title shows the selected drive. Each drive supports up to 16 files
with variable lengths and about 63 KiB of file data. Partitioning a larger
image is not supported yet.

## TinyRepo/1

The repository server uses a small custom TCP protocol, not HTTP/SHTML. A client
sends `TINY/1 GET /path\n`; the server replies `TINY/1 200 <length>\n` followed
by exactly `<length>` bytes. Start it on the host with:

```sh
python3 repo_server.py
```

It serves `repo.json` and example `.TBC` files from `repo/` on port 8080. The
guest terminal supports `PKG LIST` and `PKG INSTALL NAME`; installed programs
are saved as `.TBC` files. The repository includes HELLO, CLOCK, MATH,
TUTORIAL, NETDEMO, and FILEDEMO. Run one with `RUN MATH.TBC`. TinyBasic supports
`PRINT "text"`, `LET A = 2`, `ADD A 3`, and `SUB A 1`; variables are `A` to
`Z`, and `PRINT A` displays a variable. `FILE "NAME.TXT"` prints a disk file;
`FETCH "/repo.json"` prints a TinyRepo response. `NETDEMO.TBC` demonstrates
the network command. `FILEDEMO` reads the installed `HELLO.TBC`, so install
HELLO first. `COMP CLOCK.TBC` compiles a program to `CLOCK.BCE`; `RUN
CLOCK.BCE` executes the bytecode. Type `BSOD` in the terminal to show the blue
error screen; `R` restarts the guest, while `Esc` returns to the desktop.
You can also download a repository file directly:

```text
DOWNL 10.0.2.2/HELLO.TBC
```

The guest uses QEMU user-mode Ethernet with an NE2000 adapter; the host is
reachable at `10.0.2.2`. The browser window now uses `tiny://10.0.2.2/path`
addresses and displays repository text directly. Press Enter or click `GO` to
load a page; the small `RLD` button reloads it.

## Desktop

The desktop is named **TinyOS** and has three selectable wallpapers, a live
date/time display converted from the QEMU UTC RTC to Kyiv time, a PS/2 mouse,
Files, Notes, Terminal, TinyRepo, Settings,
Start menu, and context menu. The Start menu's Settings app changes and saves
the wallpaper. Window title bars have minimize, maximize/restore, and close
buttons; drag a lower-right corner to resize. Use `Alt+Tab` to switch windows.
Notes saves to `NOTES.TXT`; Files has clickable New, Open, and Delete buttons.
The terminal keeps an eight-command history (Up/Down) and a scrollback log
(Page Up/Page Down, Home/End). Terminal commands: `HELP`, `LS`, `CAT NAME`,
`NEW NAME.TXT`, `EDIT NAME.TXT`, `DEL NAME.TXT`, `DOWNL HOST/PATH`,
`DISK 0` / `DISK 1`, `PKG LIST`, `PKG INSTALL NAME`, `COMP NAME.TBC`, `RUN NAME.TBC` or
`RUN NAME.BCE`, `BSOD`, `NET`, `CLEAR`, and `EXIT`.
