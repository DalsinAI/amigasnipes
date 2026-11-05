Snipes for AmigaOS 3.x
======================

Snipes (SuperSet Software, 1982), the text-mode maze shooter, from David
Ellsworth's port, reverse-engineered from the original DOS program with
identical game logic, for AmigaOS 3.x on a 68040 with FPU, on acsdl12, our
SDL 1.2 for game ports (ACGame's screen, AHI sound).
Upstream: https://github.com/Davidebyzero/Snipes at caa2ce0.

Running
-------

Double-click Snipes, or run it from a Shell. It opens a 320x200 screen in
256 colours (AGA or RTG) and shows the game's 40x25 text screen on it, as
the PC's CGA did in its 40-column text mode. Needs: 68040 + FPU,
AmigaOS 3.x, AHI for sound (without it the game plays silently), about
1 MB of free memory. No data files: the game is all in the program.

At the title, type a skill level (a letter A-Z and a digit 1-9, as "A1"
or "Z9") and Return.

* Cursor keys (or keypad 8, 4, 6, 2/5): move.
* W, A, S, D (or X for down): fire, up, left, down, right. These go by
  the key's place, so they stay put on any keyboard layout.
* Space held: move at double speed (no shooting meanwhile). F1: sound on and off. F2: the sound of
  shooting on and off.
* Esc or Ctrl+C: give up the game ("Play another game? (Y or N)" follows).

Every game is recorded in the Replays drawer as
"YYYY-MM-DD HH.MM.SS.SnipesGame" (the time in UTC). To watch one, from a
Shell in the drawer:

    Snipes "Replays/2026-11-04 12.00.00.SnipesGame"

F held fast-forwards a replay. Replays from the PC build play here, and
ours there.

What is different on the Amiga
------------------------------

* Text screen. Upstream's SDL 2 build draws with a TrueType font
  (SnipesConsole.ttf, got separately) from a second thread. Here the game
  draws with a built-in 8x8 code page 437 font into an 8-bit 320x200
  screen whose palette is the CGA's 16 colours, from the game's own
  thread: whenever it waits (for the next tick of the PC's 18.2 Hz clock,
  or for a key) it reads the keys and, at most every 20 ms, redraws the
  cells that changed (amiga/acsdl_console.cpp). There is no window to
  resize.
* The font is amiga/cp437_font.h, made by amiga/tools/make_cp437_font.py
  from font8x8 (Daniel Hepper, public domain; after Marcel Sondaar's
  public domain VGA fonts), with the graphic characters it lacks (the
  faces, arrows and suits below 0x20, and a few mathematical signs) drawn
  for the port.
* Sound is the PC speaker's square wave, as upstream's: made in acsdl12's
  SDL_OpenAudio callback at 22050 Hz (upstream: 48000 Hz) with an integer
  phase step instead of a floating-point remainder for each sample
  (amiga/acsdl_sound.cpp).
* The Amiga keypad has no Num Lock, so its digits always move.
* Replays go to PROGDIR:Replays/ rather than the current directory.
* acsdl12 reports no loss of focus, so the game does not pause when its
  screen is put behind another, as upstream's does when its window loses
  focus.

Byte order
----------

The game is reverse-engineered from 8086 code and reads its data the
8086's way, a WORD at a time and as two bytes. On the 68000's byte order,
three things are changed, under `#ifdef __amigaos__`:

* A screen or maze cell (MazeTile, Snipes.h) is read as the WORD
  color << 8 | chr, as the sprite tables are written; its two bytes are
  swapped in memory here so that both views agree.
* A position (Coord, and the x, y of every object and of
  OrthoDistanceInfo, Snipes.cpp) is read as the WORD y << 8 | x (the
  original's 947 for a PL/M-86 compiler bug's leftover register): its
  bytes are swapped in the same way.
* A sprite's first WORD, SPRITE_SIZE, is read as two bytes, height then
  width: the macro puts them that way round here.
* A replay file starts with the two random seeds as little-endian WORDs
  and the skill letter and number a byte each. Upstream writes and reads
  these straight from memory, which on the Amiga would put the seeds
  big-endian and (as the skill is held in a 32-bit Uint) write the skill's
  top byte, always 0. ReadReplayHeader and WriteReplayHeader write and
  read them byte by byte, and a replay whose skill is out of range (which
  would index past the game's tables) is refused. The rest of a replay is
  a byte a frame.

platform.h: GCC's cdecl attribute means nothing on the 68000 and only
warns, so __cdecl is empty here.

Building
--------

    make -f amiga/Makefile.amigaos STOVE=... NDK=... ACSDL=...

Output: build/amigaos/Snipes/ (and its Snipes.info beside it). config.h
is made from config-sample.h, as upstream's GNUmakefile does. libgcc's
unwinder asks for POSIX thread locks: amiga/pthread_stubs.c. The stack is
128 KB (amiga/stack.c).

The icons are amiga/icons, made by AmiCradle's port_icons.py from
amiga/icons/icons.json; the art (amiga/icons/art/snipes.png, made by
amiga/tools/make_icon_art.py) is a corner of the game's maze with a snipe
and the player, in its own characters and colours.

Licence
-------

Upstream has no licence file. Its README says, under "License":

    As this is a reverse-engineered port, copyright is retained by the
    original authors of Snipes.

    This reverse-engineered source code is released with the original
    authors' permission.

The Amiga port's own files are offered under the same terms. The font
data from font8x8 is public domain. acsdl12 is MIT.

Amiga port: the AmiCradle Team.
