<!--
 - SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 -
 - SPDX-License-Identifier: GPL-3.0-or-later
-->

# C62 first flash checklist

For the first time the current firmware goes on a radio. Most of it has
only been built and unit tested, never run on a C62, so turn it on in
stages: if something breaks, you know which stage did it.

Run the commands from the workspace folder (the one that holds `OpenRTX`).
The examples use `/dev/ttyUSB0`: put your cable's port in its place
(`/dev/cu.…` on a Mac, `COM3` or similar on Windows, with `.\cskburn.exe`).
[FLASHING.md](FLASHING.md) has how to install `cskburn`, find the port and
enter burn mode on each system: do its steps 1 to 5 first.

Tick each box. When one fails, stop, save the log, and note what you did
just before.

Keys are named as in the README: **green** = Enter, **red** = ESC, the
round key = Up / Down, **side key** = the button below the PTT. Stage 4
checks that these names are right; until then, if a key does something
else, note it and carry on.

## 0. Before you start

- [ ] `cskburn` installed, `pyserial` installed (`pip install pyserial`)
- [ ] Programming cable works: `cskburn` in burn mode answers
      (FLASHING.md, step 4: radio off, hold the lower side key, power on,
      release)
- [ ] **Back up the radio's own flash**, before any flashing. It holds data
      for this unit only:

      cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --read 0x0:0x400000:backup.bin

- [ ] Copy `backup.bin` somewhere other than the laptop (cloud, USB stick)
- [ ] Your channel list, with the frequency of a strong TETRA cell you can
      test at standing still: `OpenRTX/scripts/c62/channels/ttc_tetra.h`
      for Toronto, or your own made from `example.h` (README section 6).
      Check it builds and holds what you expect:

      python3 OpenRTX/scripts/chlist.py my_channels.h -o channels.bin
      python3 OpenRTX/scripts/chlist.py --show channels.bin


To go back to the stock firmware at any point, flash the backup at 0. It
covers the whole flash, so it also removes the channel list and the saved
settings:

    cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --verify-all 0x000000 backup.bin

Never press the main PTT while flashing: it shares the boot ROM's line.

## Building and flashing a stage

Each stage below gives the build options. Build, then flash:

    docker run --rm -it --user "$(id -u):$(id -g)" -v "$PWD":/work \
        openrtx-c62 c62-build -DC62_CHANNELS=scripts/c62/channels/ttc_tetra.h \
        <stage options>

(put your own list after `-DC62_CHANNELS=`, relative to the `OpenRTX`
folder.)

    cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --verify-all \
        0x100000 OpenRTX/build_c62/zephyr/dsp_firmware.bin \
        0x2c0000 OpenRTX/build_c62/zephyr/channels.bin \
        0x000000 OpenRTX/build_c62/zephyr/zephyr.bin

`C62_CHANNELS` builds the channel list image `channels.bin` next to the
firmware. The list only needs flashing the first time and when it
changes: from stage 2 on, the `0x2c0000` line can be left out. Enter burn
mode again before every `cskburn` command.

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
- [ ] FM: a known FM signal is heard (side key + 5 changes mode)

Channel list (printed at boot, near the `TETRA log` line):

- [ ] `Channel list: N channels, N TETRA names` at boot, with the numbers
      `--show` gave. `none flashed` or `not used` means the image is
      missing or damaged: see below
- [ ] **Red** switches to memory mode, which shows your channels; **Up** /
      **Down** step through them

T mode on the strong cell (memory mode, its channel):
- [ ] `start on <frequency>, filter All` in the log
- [ ] `sync locked` within a few seconds, and the screen shows the station
- [ ] A `cell` line with your network's MCC-MNC (302-2013 for the TTC) and
      a location area
- [ ] A `signal` line every 10 s: `locked`, phase error about 20% or
      less on a strong signal once the frequency correction has settled
      (a C62 measured 13 to 17% with a test signal; above 45% the meter
      shows no decoding), and `crc ok` higher on each line
- [ ] `correction` settles within about 20 s, usually somewhere around
      +1600 Hz plus your radio's crystal error, then changes little. After
      the radio has been on for 10 s more, turn it off and on: `correction`
      starts at that value
- [ ] `reacquired` and `realigned` stay the same from line to line, or
      go up only now and then. Climbing every line means the receiver
      keeps losing the signal's timing: keep the log
- [ ] **No `samples lost: resync` lines**, or only a rare one. Many of them
      mean the radio cannot keep up: note how often, stop here
- [ ] **Menu** > **Field test**: the same network, location area and main
      carrier as the log; neighbours listed after a few seconds; the
      network time matches the clock
- [ ] `call … setup` lines when the network is busy, with the talkgroup
      names of your list
- [ ] Speech heard for a clear call on the main carrier, without chopping
- [ ] Leave it 15 minutes: no crash or reboot (a reboot shows the
      `TETRA log` line again)
- [ ] Settings are kept: change the brightness (side key + 7 / 8), be on
      your TETRA channel in T mode, turn the knob off, wait 5 s, turn it
      on. Same brightness, and it starts on that frequency in T mode (in
      VFO mode). If it starts on 430.000 M17 instead, the save failed: note
      any `nvmem:` line in the log
- [ ] Turn off and on twice more, changing the brightness each time: the
      last one is kept each time

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

- [ ] **Settings** > **TETRA** > Follow calls OFF: the next call on
      another carrier shows `following off` in the log and the radio stays
      home. Back ON afterwards

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

- [ ] **Settings** > **TETRA** > Cell tracking OFF: no `roam:` lines while
      standing still; a long **0** still searches once. Back ON afterwards

Moving (a ride, with the laptop):

- [ ] Between stations: `roam: weak cell`, `measuring`, then moves to a
      stronger neighbour, and the station name on the screen follows
- [ ] In a dead zone: `roam: no lock, measuring`, then it finds a cell
      again when the signal returns
- [ ] No moving back and forth between two cells every few seconds

## 4. Keys

- [ ] Which key is which: **green** opens the menu (Enter), **red** goes
      back and switches VFO / memory mode (ESC), the round key steps Up /
      Down, the **side key** below the PTT works the shortcuts (side key +
      number). If not, note what each key does instead, including **P1**
      and **P2**: the README's names, and maybe the key handling, need
      fixing

In memory mode:

- [ ] Short **1**–**9** type a channel number (amber on screen); **green**
      jumps, **red** cancels, it jumps on its own after 2 s
- [ ] Channel 10, 20…: in T mode the **0** works once a number is started
- [ ] A number that is not a channel: nothing changes
- [ ] Long **5** changes mode; long **6** power; long **7** / **8**
      brightness
- [ ] Long **9** locks the keypad, long **9** again unlocks it
- [ ] **Menu** > **Field test** scrolls with Up / Down, **green** searches,
      **red** goes back
- [ ] **Settings** > **TETRA**: **green** on a line, then Up / Down switch
      it on and off, **green** or **red** finishes
- [ ] Keep filter: pick a filter with **#**, wait 10 s, turn off and on:
      it starts on that filter. With Keep filter OFF it starts at `TG All`
- [ ] Settings are kept: change the brightness, wait 10 s, turn off and
      on
- [ ] **Menu** > **About**: `C62 fork:`, `Jackson Zhou` and `jkzhou.ca`
      first, then the OpenRTX authors
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

## 6. FM

Same firmware, no new build. FM, on a local repeater or simplex channel:

- [ ] Long **5** until FM: line 1 shows width and power; with an
      `FM_CHANNEL` repeater in the list, its tones and shift (yellow)
- [ ] A signal opens the squelch: the name or frequency turns green,
      `Receiving S…` below it, audio plays; the yellow tick on SIG moves
      with **Settings** > **FM** > Squelch
- [ ] **Settings** > **FM**: Width, Power, Tone mode, TX and RX tone change
      with Up / Down and show on line 1
- [ ] If you are licensed for it: PTT turns the screen red, MIC moves, a
      second radio hears you (with the tone, if set)
- [ ] **Settings** > **Memory**: save the VFO to M1, rename it, turn off and
      on: it is still there, after the list's channels in memory mode;
      Delete (green twice) clears it
- [ ] Fast copy: hold **\*** in FM, key a second radio next to the C62
      (set to a tone or DCS code): the frequency, then the tone or code,
      appear; **green** copies them into the VFO
- [ ] Scan: squelch above Off, hold **0** in memory mode: `Scan up`, the
      channels change about 10 times a second; key a second radio on one of
      them: it stops there and plays it, then goes on 2 s after you let go
- [ ] If you are licensed for it: **Settings** > **FM** > VOX 5, talk into
      the radio on a channel a second radio listens to: the screen turns red
      and it transmits, then stops 1 s after you stop. Note which level keys
      up reliably without keying on background noise
- [ ] AI noise reduction: **Settings** > **FM** > AI denoise RX 5, listen
      to a weak, hissy FM signal: the hiss drops, the voice stays. Note the
      percentage shown next to the level (the processor time it takes); if
      it says `slow`, it could not keep up and switched itself off
- [ ] FM radio: **Menu** > **FM radio**, tune a local station (type it,
      or hold Up to seek): music from the speaker. If it stays silent, note
      it: the BK1080's audio path is the unknown. Key a second radio on the
      FM channel meanwhile: the radio goes quiet and the channel is heard
- [ ] Saved calls: **Settings** > **TETRA** > Save calls on, let a call
      or two go by in T mode, then **Menu** > **Saved calls**: they are
      listed with talkgroup, date and time; **green** plays one. Turn the
      radio off and on: they are still there
- [ ] Instant replay: after a transmission on FM or T, press **P2**: it
      plays again, `Replay 1/1` at the top; **P2** twice goes back further
- [ ] If you are licensed for it: DTMF, hold PTT and press **1** **2** **3**
      next to a second radio (or a DTMF decoder app): the tones are heard
      and decoded, `DTMF 123` on the screen
- [ ] Spectrum: side key + **0** in FM, key a second radio within the
      view: a bar rises at its frequency; **green** tunes the VFO to it

## If something goes wrong

- **`cskburn` fails before writing anything:** check the cable and burn
  mode first. Note the cable's make and its chip, and the exact error.
- **`Channel list: none flashed`:** nothing valid at `0x2c0000`; flash
  `channels.bin` there. **`not used, damaged or other version (-N)`:** the
  image was cut short or made by another version of `chlist.py`: build it
  again with the `chlist.py` of this firmware and flash it again. The radio
  works without a list, with no memory channels and numbers instead of
  names.
- **Settings forgotten at every start** (after waiting 10 s before turning
  off): the settings flash area cannot be written. Note it.
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
