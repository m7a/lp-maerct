---
section: 11
x-masysma-name: maerct
title: Ma_Sys.ma Emergency Remote Control
date: 2020/01/17 15:22:37
lang: en-US
author: ["Linux-Fan, Ma_Sys.ma (Ma_Sys.ma@web.de)"]
keywords: ["programs", "c", "maerct", "linux", "keyboard"]
x-masysma-version: 1.1.0
x-masysma-repository: https://www.github.com/m7a/lp-maerct
x-masysma-website: https://masysma.lima-city.de/11/maerct.xhtml
x-masysma-owned: 1
x-masysma-copyright: |
  Copyright (c) 2020 Ma_Sys.ma.
  For further info send an e-mail to Ma_Sys.ma@web.de.
---
Name
====

`maerct` -- Ma_Sys.ma Emergency Remote Control: Kill X11 in case of hangs.

Synopsis
========

	maerct [-f] FILE

Description
===========

This program has to be run by the root user. It takes a Linux keyboard event
device, e. g. `/dev/input/event0` and grabs it exclusively. Do not run it if
you only have one keyboard attached. `-f` forks twice.

## Table of blink codes

NumLock  CapsLock  ScrollLock  Description
-------  --------  ----------  ------------------------------
slow     off       off         Program operating correctly.
fast     off       off         Program expecting input.
off      fast      off         Process `X` takes >80% CPU.
off      slow      off         RAM exceeded.
off      off       slow        Shorttime Load AVG greater 10.
off      off       fast        Command is being executed.
off      off       off         Program not operating.

## Table of possible inputs

Key  Description
---  ----------------------------------------------------------
ESC  Cancel pending operation
F1   Attempt to kill X11 process
F2   Attempt to start (and immediately terminate) new X server.
F4   Exit this program (no unlock required)
u    Press this before another operation to unlock input.

Extended Description
====================

This program is intended to run as a background service on an interactively
used machine with a second keyboard connected for the sole purpose of being
used by this program (e. g. one can install otherwise unused PS2 keyboards for
the purpose).

It then outputs a limited amount of status niformation in form of regular blink
codes using the three keyboard LEDs: NumLock, CapsLock and ScrollLock.

Apart from displaying status, it can serve as a remote control (thus the name)
for killing the X-server. This might be an interesting feature for users of
third-party kernel modules (e. g. NVidia drivers or VirtualBox modules) which
used to cause some X11 freezes on the machine this program was developed for.
By pressing [U], followed by [F1], one can cause the X-server to be
terminated, freeing access to the console (if it does not work, sequence [U]
followed by [F2] attempts to restart the X-server).

Be aware that similar to SysRq-codes (which may also work in such cases), this
program allows bypassing any running screensaver! In case you only want the
keyboard blinking, consider using [ma_capsblinker(11)](ma_capsblinker.xhtml)
(currently does not provide any status information, but does not need to run
on a _separate_ keyboard).

This program is provided together with an LSB init script
(`masysma-emergency-remote-control`) and a default configuration in
`/etc/default/maerct` which is actually a shellscript sourced into the init
script, allowing for host-specific configuration or any other logic.

Compilation
===========

To compile this program, a C compiler is needed e. g. as provided by Debian
package `gcc-8-base`. Additionally, the `ant` build tool is needed and can then
be invoked by running `ant`. To build the Debian package (with the necessary
dependencies installed), use `ant package`.
