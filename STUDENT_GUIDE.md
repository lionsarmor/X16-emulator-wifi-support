# Student guide: building, running, and sharing an X16 app

This is a beginner-friendly walkthrough for this project. If you've never
used `git`, `cmake`, or a terminal much before, start here. If you're
already comfortable with those, the main [README.md](README.md) and
[EXPORTING.md](EXPORTING.md) are faster reading.

## What is this, in plain terms?

The **Commander X16** is a modern 8-bit computer, built to feel like a
machine from the 1980s but designed today. You can run it as real hardware,
or as an **emulator** — a program that pretends to be an X16 on your
regular computer, so you can test X16 software without owning one.

This particular copy of the emulator ("this fork") adds one extra thing the
official emulator doesn't have: emulation of a real WiFi expansion card
people build for the X16. That means X16 programs that talk to the
internet — chat apps, weather apps, anything with a network feature — can
be built and tested right here, with your computer's own internet
connection standing in for the card's.

By the end of this guide you'll be able to:

1. Build the emulator from source.
2. Run an X16 program in it, with WiFi turned on.
3. Package a finished X16 program into something that runs like a normal
   app on Linux or Windows, or that a real X16 owner could put on real
   hardware.

## What you'll need

- A computer running Linux (this guide uses Ubuntu/Debian commands —
  adjust the package manager step if you're on something else).
- Basic comfort opening a terminal and running commands.
- About 15 minutes and an internet connection.

Install the build tools:

```sh
sudo apt update
sudo apt install git cmake build-essential libsdl2-dev python3
```

## Step 1: Get the code

```sh
git clone https://github.com/lionsarmor/X16-emulator-wifi-support.git
cd X16-emulator-wifi-support
```

## Step 2: Build the emulator

```sh
cd x16-emulator
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This produces a program called `x16emu` inside the `build/` folder. That's
the emulator itself.

## Step 3: Get the system ROM

A real X16 needs an operating system to boot into, called the **ROM**
(short for read-only memory — the chip that holds it on real hardware).
The emulator needs a copy of that same ROM as a file.

Download the latest `rom.bin` from
[X16Community/x16-rom's releases page](https://github.com/X16Community/x16-rom/releases)
and place it in the `build/` folder, next to `x16emu`.

## Step 4: Run it

```sh
./build/x16emu
```

You should see an X16 boot up and land on a `READY.` prompt — that's
Commander BASIC, the machine's built-in programming language. This is the
plain emulator with no WiFi card, exactly like the official one.

Close the window (or press `Ctrl+C` in the terminal) when you're done
looking.

## Step 5: Turn on WiFi

The WiFi card doesn't exist unless you ask for it. Add `-wifi`:

```sh
./build/x16emu -wifi
```

Nothing will look different yet — you won't see a WiFi light or a menu.
The card is there now, in the background, waiting for a program to talk to
it. A plain BASIC session has no reason to notice it.

## Step 6: Try a real WiFi app

To actually see the WiFi card do something, you need a program built to
use it. [DESK COMMANDER](https://github.com/lionsarmor/DESK-COMMANDER) is
a real X16 desktop app with a network chat feature that this emulator has
been tested against. Grab a copy of its already-built files:

```sh
cd ..
git clone https://github.com/lionsarmor/DESK-COMMANDER.git
```

Then launch it inside your WiFi-enabled emulator, pointing at its `dist/sdcard`
folder (the set of files it needs, as if they were on an SD card):

```sh
x16-emulator/build/x16emu \
    -rom x16-emulator/build/rom.bin \
    -wifi \
    -fsroot DESK-COMMANDER/dist/sdcard \
    -prg DESK-COMMANDER/dist/sdcard/DCMAIN.PRG \
    -run -rtc -scale 2
```

That's the same pattern any X16 WiFi app follows: point `-fsroot` at the
app's files, `-prg ... -run` at its main program, and add `-wifi` so the
network card is actually there for it to find.

## Step 7 (bonus): Share something you've built

If you've built your own X16 program and want to hand it to someone who
doesn't want to install any of this themselves, this repo can package it
into a single file that just works — a double-click app on Windows, a
one-command launch on Linux, and a copy-to-SD-card folder for anyone with
real X16 hardware and a real WiFi card. That's a separate tool
(`tools/bundle-x16-app.sh`), covered step by step in
[EXPORTING.md](EXPORTING.md). The short version:

```sh
tools/bundle-x16-app.sh --name "MY APP" --prg MYAPP.PRG --sdcard /path/to/my/app/files
```

## Cheat sheet

| I want to... | Command |
|---|---|
| Build the emulator | `cmake -S . -B build && cmake --build build -j` (run inside `x16-emulator/`) |
| Run plain, no WiFi | `./build/x16emu` |
| Run with WiFi | `./build/x16emu -wifi` |
| Run a specific app with WiFi | `./build/x16emu -wifi -fsroot <app folder> -prg <app folder>/<file>.PRG -run` |
| Package an app to share | `tools/bundle-x16-app.sh --name "..." --prg ... --sdcard ...` |

## Glossary

- **Emulator** — a program that pretends to be a different computer, so
  software for that computer runs on yours instead.
- **ROM** — the built-in operating system a computer boots into. On real
  hardware this lives on a physical chip; here it's a file (`rom.bin`).
- **PRG** — an X16 program file, like a `.exe` is on Windows.
- **fsroot / "SD card"** — the folder of files the X16 sees as its storage
  drive. On real hardware this is an actual SD card; the emulator can use
  a normal folder on your computer instead (`-fsroot`).
- **KERNAL** — the lowest-level part of the X16's operating system (the
  spelling is intentional — it's a Commodore-computer tradition this
  machine continues).
- **AT commands** — the WiFi card understands old modem-style text
  commands (`ATD` to dial, `ATW` to scan/join WiFi, etc.), the same
  language real dial-up modems used decades ago. You won't usually type
  these yourself; the X16 program you're running does it for you.

## Where to go from here

- Main [README.md](README.md) — what the WiFi card emulates and how, in
  technical detail.
- [EXPORTING.md](EXPORTING.md) — full reference for packaging an app for
  Linux, Windows, and real hardware.
- [x16-emulator/quickstart-linux.md](x16-emulator/quickstart-linux.md) —
  the upstream emulator's own general getting-started guide (not
  WiFi-specific).
