Process memory watcher utility for fun and profit.

There are similar tools incorporated in some debuggers or cheat code finders, but few of them seem to provide convenient way to scroll memory around per-byte, adjust window width to align bytes or mark differences between snapshots. None of them combine these facilities into single tool, thus here're my 5 cents.

Features
=======

* Highlights changes between snapshots with color coding for age and type
* Grayed out zeroes
* Terminal window resizing when you do not expect it
* On-the-fly per-byte, per-row, per-screen memory navigation
* On-the-fly buffer resizing
* Relative addressing with adjustment

Shortcuts
========

* Arrow keys — Scroll memory by bytes and byte rows
* PgUp/PgDn — Scroll memory by buffer size
* Home — Jump to relative 0x0 address
* Space — Reset diff mask
* [/] — Add/remove columns
* ,/. — Resize watch buffer by bytes
* </> — Resize watch buffer by rows
* ;/' — Adjust current relative address display without scrolling
* R — Set current location as 0x0
* Q — Quit

Caveats
======

This produces quite a lot of traffic for your poor terminal to handle, about 715kbps for 0x200 bytes of input data. VTE-based terminals do not seem to handle that well, failing to clear some of the lines. Xterm, kmscon, linux console however do handle that much properly.
