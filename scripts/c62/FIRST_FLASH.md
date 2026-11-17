# C62 first flash checklist

For the first time the current firmware goes on a radio. Most of it has
only been built and unit tested, never run on a C62, so turn it on in
stages: if something breaks, you know which stage did it.

Run the commands from the workspace folder (the one that holds `OpenRTX`).
On a Mac the cable shows up as `/dev/tty.usbserial-…` or similar
(`ls /dev/tty.*`); the examples use `/dev/ttyUSB0`.

Tick each box. When one fails, stop, save the log, and note what you did
just before.

## 0. Before you start

- [ ] `cskburn` installed, `pyserial` installed (`pip install pyserial`)
- [ ] The cable is an **FT232R** (USB ID `0403:6001`), not just "FTDI":
      an FT231X (`0403:6015`, e.g. BTECH PC03) is reported to fail with
      `E5003: Failed to load RAM burner`. How to check: README section 2
- [ ] Programming cable works: `cskburn` in burn mode answers (README
      section 2: radio off, hold the lower side key, power on, release)
- [ ] **Back up the radio's own flash**, before any flashing. It holds data
      for this unit only:

      cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --read 0x0:0x400000:backup.bin

- [ ] Copy `backup.bin` somewhere other than the laptop (cloud, USB stick)
- [ ] A strong TTC cell you can test at, standing still (a station or near
      one), and its frequency in the channel list

To go back to the stock firmware at any point, flash the backup at 0:

    cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --verify-all 0x000000 backup.bin

Never press the main PTT while flashing: it shares the boot ROM's line.

## Building and flashing a stage

Each stage below gives the build options. Build, then flash:

    docker run --rm -it --user "$(id -u):$(id -g)" -v "$PWD":/work \
        openrtx-c62 c62-build -DC62_CHANNELS=scripts/c62/channels/ttc_tetra.h \
        <stage options>

    cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --verify-all \
        0x100000 OpenRTX/build_c62/zephyr/dsp_firmware.bin \
        0x000000 OpenRTX/build_c62/zephyr/zephyr.bin

Then start the log and leave it running for the whole stage:

    python3 OpenRTX/scripts/c62_log.py -p /dev/ttyUSB0 -o stage1.log

Rename the file for each stage. Build options are remembered, so each
stage passes all the ones that change.

## 1. Receive only: no following, no cell tracking

Options: `-DC62_TETRA_LOG=ON -DC62_TETRA_FOLLOW=OFF -DC62_TETRA_ROAM=OFF`

Boot and basics:

- [ ] Boots to the main screen, display and backlight fine
- [ ] Keys respond; volume knob works
- [ ] The log shows `TETRA log, 256 kB kept, side key + 0 sends them`
      (if nothing at all appears, see "No log" below)
- [ ] FM: a known FM signal is heard (side key + 5 to change mode)

T mode on the strong cell (memory mode, its channel):

- [ ] `start on <frequency>, filter All` in the log
- [ ] `sync locked` within a few seconds, and the screen shows the station
- [ ] A `cell` line with MCC-MNC 302-2013 and the right location area
- [ ] A `signal` line every 10 s: `locked`, phase error mostly 10% or
      less (above 45% the meter shows no decoding)
- [ ] **No `samples lost: resync` lines**, or only a rare one. Many of them
      mean the radio cannot keep up: note how often, stop here
- [ ] **Menu** > **Field test**: the same network, location area and main
      carrier as the log; neighbours listed after a few seconds; the
      network time matches the clock
- [ ] `call … setup` lines when the network is busy, with talkgroup names
- [ ] Speech heard for a clear call on the main carrier, without chopping
- [ ] Leave it 15 minutes: no crash or reboot (a reboot shows the
      `TETRA log` line again)
- [ ] Settings are kept: change the brightness, be on the TTC channel in T
      mode, turn the knob off, wait 5 s, turn it on. Same brightness, and
      it starts on that frequency in T mode (in VFO mode). If it starts on
      430.000 M17 instead, the save failed: note any `nvmem:` line in the
      log

## 2. Call following

Options: `-DC62_TETRA_FOLLOW=ON`

- [ ] A call on another carrier shows `retune: call N to carrier M slots …
      >> <frequency>`, then `voice call N`
- [ ] Its speech is heard
- [ ] After the call, `retune: main carrier` or `follow: back to the main
      carrier`, then the lock returns (`sync locked`) within a second or two
- [ ] Encrypted calls are not followed (`declined`), the radio stays home
- [ ] `filtered out` calls are not followed when a filter is set (# / \*)
- [ ] Still no `samples lost` lines during calls

If following misbehaves, save the log around it: the `retune` lines and
what came before them are what is needed to fix it.

## 3. Cell tracking and the best signal search

Options: `-DC62_TETRA_ROAM=ON`

Standing still:

- [ ] Long **0**: `roam: search asked, measuring N carriers`, the screen
      shows `scanning`, then `roam: measured …` with signal levels
- [ ] The levels look plausible: the cell you are on is near the top,
      far stations are weaker. All the same value, or all very low, means
      the 5 ms measurement is too short: note it
- [ ] Then `roam: staying on …`, or a move with `roam: try >>` and
      `roam: locked on …` within a few seconds
- [ ] Audio of a call in progress clicks at most during the search
- [ ] **Field test** shows the measurement (`Measured …s ago`), the
      cell received in green with a `*`, and `Tracking: on the cell`

Moving (a ride, with the laptop):

- [ ] Between stations: `roam: weak cell`, `measuring`, then moves to a
      stronger neighbour, and the station name on the screen follows
- [ ] In a dead zone: `roam: no lock, measuring`, then it finds a cell
      again when the signal returns
- [ ] No moving back and forth between two cells every few seconds

## 4. Keys

In memory mode:

- [ ] Short **1**–**9** type a channel number (amber on screen); Enter
      jumps, ESC cancels, it jumps on its own after 2 s
- [ ] Channel 10, 20…: in T mode the **0** works once a number is started
- [ ] A number that is not a channel: nothing changes
- [ ] Long **5** changes mode; long **6** power; long **7** / **8**
      brightness
- [ ] Long **9** locks the keypad, long **9** again unlocks it
- [ ] In T mode: short **0** opens the recent calls (on release), long
      **0** searches; short **#** / **\*** change the filter (on release),
      long **#** holds a talkgroup

In VFO mode:

- [ ] Typing a frequency still works, including 5s
- [ ] Long **5** as the first digit changes mode instead

## 5. The kept log

- [ ] Side key + **0**: `--- stored log, N bytes ---`, the lines, then
      `--- end of stored log ---`, in about 25 s for a full log
- [ ] A ride with nothing connected, then connect and side key + 0: the
      ride is there, `network time` lines give the clock time

## If something goes wrong

- **`cskburn` handshakes, then `E5003: Failed to load RAM burner`:** the
  cable, not the firmware. Check it is an FT232R (`0403:6001`), see
  README section 2.
- **No log at all:** check the port and 115200 baud. If the radio works
  otherwise, build with `-DC62_TETRA_LOG=OFF`: if the radio now works
  better, the log is the problem.
- **Reboots or freezes in T mode:** note what was happening (a call, a
  search, a key). A stack overflow is a likely cause; it can be fixed by
  giving the receiver thread more stack.
- **Chopping audio or `samples lost`:** try the stage again with
  `-DC62_TETRA_SPEECH=OFF` (no speech, decoding only) to see whether the
  speech decoder is what does not fit in time.
- **Anything else:** keep the stage's log file and say which box failed.
  The log is what tells what the radio was doing.
