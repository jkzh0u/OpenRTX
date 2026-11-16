<!--
 - SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 -
 - SPDX-License-Identifier: GPL-3.0-or-later
-->

# Retevis C62: OpenRTX with a TETRA receiver

OpenRTX for the Retevis C62, with a TETRA receiver. This build:

- receives **FM** and **M17**, and **TETRA** downlinks ("T" mode): it decodes
  calls, plays clear speech, shows short messages, follows calls to other
  carriers and moves between cells of the network by itself (section 5);
- can hold a **channel list** built into the firmware, with **talkgroup
  names** and **filters** chosen on the radio (section 6). The list for the
  Toronto Transit Commission network is included;
- has a **recent calls** list with details, a **last heard** line, **talk
  time**, a talkgroup **hold**, **alarm** alerts, a decoding quality meter
  and colour in T mode, and plays one call at a time;
- can stream the raw receiver audio over the programming cable (section 3);
- builds with Docker (section 1), and its screens can be previewed on a
  computer (section 7).

It is receive only in T mode, and cannot store settings yet: it starts in
VFO mode on 430.000 MHz, M17, filter `TG All`, after every power on.

## Quick reference: buttons

On the main screen, in any mode:

| Button | Does |
|---|---|
| **Menu** (Enter) | Open the menu. The **Channels** entry lists the built-in channels |
| **ESC** | Switch between VFO mode (a frequency) and memory mode (the channel list) |
| **Up** / **Down** | VFO: step the frequency. Memory: next / previous channel |
| **1** to **9** | VFO: type a frequency, then **Enter** (in T mode **0** opens the recent calls instead) |
| Side key + number | Shortcut, see below |

Shortcuts: hold the **side key** and press a number. Long-press the side key
on its own to latch shortcut mode, so the numbers work without holding it;
press it again to unlatch.

| Side key + | Does |
|---|---|
| **5** | Change mode: FM, M17, T (TETRA) |
| **1** / **2** / **3** | FM: CTCSS off, encode, decode or both / tone down / tone up |
| **4** | FM: bandwidth 12.5 or 25 kHz |
| **6** | Transmit power |
| **7** / **8** | Brightness down / up |
| **9** | Lock or unlock the keypad |

In T mode, on the main screen:

| Button | Short press | Long press |
|---|---|---|
| **#** | Next talkgroup filter, or release a held talkgroup | Hold the current call's talkgroup, or the last one heard. Again to release |
| **\*** | Previous talkgroup filter, or release a held talkgroup | Line 1: switch between the last call heard and the signal figures |
| **0** | Recent calls | |

The recent calls list shows how long ago each call was on the right,
alarms in red and a lock before encrypted calls. **Up** / **Down** to
scroll, **Enter** to see the
selected call in detail (who talked last, how many times it was heard, when
it was first heard), **ESC** or **0** to go back. In the detail, **Enter**
holds that talkgroup and **ESC** goes back to the list.

A short press of **#** or **\*** acts when you let go of the key, so that
holding it does only the long press.

## 1. Build with Docker

You only need Docker and git. The image holds the toolchain (Zephyr SDK
0.16.8, `west`, CMake, Ninja). Your source tree stays on your disk and is
mounted into the container. ListenAI's `lisa` tool is not needed.

The C62 builds against ListenAI's Zephyr 3.4 fork. `west` puts it, and the
CSK6 modules, next to the OpenRTX checkout, so make a folder to hold both:

```bash
mkdir c62-ws && cd c62-ws
git clone -b c62/tetra https://github.com/sp5wwp/OpenRTX.git

# Build the image once (5-10 minutes, ~2 GB)
docker build -t openrtx-c62 OpenRTX/scripts/c62

# Build the firmware. The first run downloads the Zephyr fork (~1.5 min).
docker run --rm -it --user "$(id -u):$(id -g)" \
    -v "$PWD":/work openrtx-c62
```

The outputs are:

| File | Flash at |
|---|---|
| `OpenRTX/build_c62/zephyr/zephyr.bin` | `0x000000` |
| `OpenRTX/build_c62/zephyr/dsp_firmware.bin` (downloaded, hash-checked) | `0x100000` |

Run the same `docker run` command after each code change. The build is
incremental.

**Build options** go after the image name, for example:

```bash
docker run --rm -it --user "$(id -u):$(id -g)" \
    -v "$PWD":/work openrtx-c62 c62-build -DC62_REG43_SWEEP=ON
```

| Option | Default | Effect |
|---|---|---|
| `C62_WIDE_RX` | `ON` | M17-mode RX uses BK4819 `REG_43 = 0x7e28`, the widest documented filter (see section 4) |
| `C62_BASEBAND_DUMP` | `ON` | Send raw 48 kHz RX audio over the UART. The console log is muted after audio starts |
| `C62_BASEBAND_DUMP_BAUD` | `115200` | UART baud rate during the dump |
| `C62_BASEBAND_DUMP_BURST` | `ON` | Record 1 MiB (10.4 s) to PSRAM, send it, repeat |
| `C62_BASEBAND_DUMP_FORMAT` | `1` | `1` = 16-bit samples, `2` = 12-bit packed (13.9 s per burst) |
| `C62_TETRA` | `ON` | TETRA receive mode "T" (section 5), with libtetra |
| `C62_TETRA_SPEECH` | `ON` | Play clear TETRA speech, with OpenACELP (section 5) |
| `C62_TETRA_FOLLOW` | `ON` | Follow TETRA calls to the other carriers of a cell (section 5) |
| `C62_TETRA_ROAM` | `ON` | Move to the best TETRA cell as the radio moves, search when the signal is lost (section 5) |
| `C62_CHANNELS` | empty | Channel list built into the firmware, for memory mode (section 6) |
| `C62_REG43_SWEEP` | `OFF` | Cycle `REG_43` through 7 test values in M17 RX |
| `C62_REG43_SWEEP_MS` | `1400` | Recorded time per sweep value (one burst covers all 7 values) |

CMake remembers options in `build_c62`. To drop one, pass it again with its
default value or delete `OpenRTX/build_c62`.

Notes:

- Windows: run this from a WSL2 shell, with the folder inside WSL.
- Apple silicon: the image builds natively for arm64.
- Without Docker: the same steps are in `build.sh`. Install the Zephyr SDK
  0.16.8 `arm-zephyr-eabi` toolchain, `west`, `cmake`, `ninja`, `gperf`,
  `dtc` and `meson`, then follow the script.

## 2. Flash

Use [`cskburn`](https://github.com/LISTENAI/cskburn/releases) over the
Kenwood programming cable. To enter burn mode: radio off, hold the lower
side key, power on, release the key. The screen stays dark. Don't press the
main PTT while flashing: it shares the boot ROM's UART RX line. `cskburn`
resets the radio after each command, so enter burn mode again before each
one.

```bash
# Once, before anything else: back up your radio's own flash (it is per-unit)
cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --read 0x0:0x400000:backup.bin

cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --verify-all \
    0x100000 OpenRTX/build_c62/zephyr/dsp_firmware.bin \
    0x000000 OpenRTX/build_c62/zephyr/zephyr.bin
```

## 3. Capture the RX baseband

The dump uses the UART TX line on the Kenwood connector, which is the line
your programming cable receives on. Put the radio in **M17 mode** on the
wanted frequency. M17 mode applies the flat audio path: AF bypass, no
emphasis, no DC filters and the wide `REG_43`. Then:

```bash
pip install pyserial
python3 OpenRTX/scripts/c62_bbdump.py -p /dev/ttyUSB0 -t 200 -o tetra
```

A 115200-baud cable moves about 11.5 kB/s, and 48 kHz 16-bit audio is
96 kB/s, so the radio works in bursts. It records 10.4 s without gaps into
PSRAM, then takes about 91 s to send them. Recording starts again once the
burst is sent. `-t 200` gets about two bursts. Each burst becomes its own
file set:

- `tetra_burst001.wav`: 48 kHz mono 16-bit, for listening, sox or Audacity.
- `tetra_burst001.f32`: float32 in [-1, 1), for a GNU Radio *File Source*
  (float, 48 kHz).
- `tetra_burst001.csv`: per 5 ms packet, the sample index, `REG_43` value,
  RSSI (dBm) and flags.

The summary line reports peak and RMS level and a count of clipped samples.
**Check that clipping is 0 on a real TETRA signal.** Its discriminator output
swings much further than the ±2 kHz test tones. If it clips, lower the BK4819
RX gain (`REG_48`, in `m17Fields` in `radio_C62.cpp`) or `C62_ADC_RX_GAIN_*`
in `hwconfig.h`.

Use `--raw capture.bin` to also keep the raw bytes, and `-i capture.bin` to
parse them again later. Stray bytes from the boot log or the DSP are skipped
by resynchronising on the packet header and checking a CRC. The packet format
is documented in `platform/drivers/audio/baseband_dump_c62.h`.

With a faster USB-UART adapter (CH340, FT232R, CP2102N), build with
`-DC62_BASEBAND_DUMP_BURST=OFF -DC62_BASEBAND_DUMP_BAUD=1500000` for a
continuous stream. Pass the same `-b` to the script. 921600 baud is enough
with `-DC62_BASEBAND_DUMP_FORMAT=2`.

## 4. RX bandwidth (BK4819 REG_43)

From the BK4819 V3 register list:

| Bits | Field |
|---|---|
| `<14:12>` | RF (channel) filter bandwidth: 1.7, 2.0, 2.5, 3.0, 3.75, 4.0, 4.25, 4.5 kHz |
| `<11:9>` | Same filter, used instead when the signal is **weak** |
| `<8:6>` | AF TX LPF2 (TX only) |
| `<5:4>` | BW mode: 0 = 12.5 kHz, 1 = 6.25 kHz, 2 = 25 kHz. Bit 5 doubles the filter bandwidth and the IF |
| `<3>` | Undocumented, set in every known firmware |
| `<2>` | +6 dB after the FM demodulator |

The measured `0x7828` (25 kHz mode) left `<11:9>` at 3.75 kHz. The chip then
narrows the channel filter on weak signals, which is when a wide-band digital
signal can least afford it. `C62_WIDE_RX` uses `0x7e28` (4.5 kHz × 2 in both
cases), the widest documented setting. Above that are only undocumented bits.
`C62_REG43_SWEEP` measures them with your multitone setup. Each burst then
holds 1.4 s each of `7808 7828 7e28 7e2c 7e38 7e20 fe28`. Split them with
`c62_bbdump.py ... --split-reg43` and skip about 50 ms at each value change.

## 5. TETRA mode

Hold the **side key** and press **5** to change the mode: FM, M17, T. Or
pick a TETRA channel of the channel list (section 6), which starts T mode
by itself. In **T** mode the radio receives a TETRA TMO downlink (a base
station, pi/4-DQPSK) and decodes it on the radio with
[libtetra](https://github.com/sp5wwp/libtetra). It is receive only: PTT does
nothing. Tune to the base station's downlink frequency as usual, or to any
cell of the network: cell tracking finds the best one.

The main screen shows, in VFO and memory mode alike:

| State | Line 1 | Large | Small, grey |
|---|---|---|---|
| No signal | Last call heard, or demodulator figures | `T searching`, or `T scanning` while looking for another cell | The channel being tried, else the frequency |
| Locked to a cell | Last SDS for 30 s: its sender by name in white, then its text scrolling in yellow; else last call heard, or demodulator figures | `T <location>` | Frequency received, after `Ch <n>` in memory mode; `enc` on the right if the cell encrypts |

The station is drawn in the largest font that fits. During a call the
screen shows instead, from the top:

1. The called group or radio, large.
2. The talking party, or `unknown` in grey.
3. The location.
4. A loudspeaker and the length of this transmission, e.g. `0:12`, or
   `Idle`, or a lock and `Encrypted`.

Talkgroups and radios show by name when the channel list names them
(section 6), the location as a named location area, else the channel of the
list on this cell's main carrier, else `LA <n>`. Colours: talkgroups sky
blue, locations white beside a green radio mast, the talk time amber, idle
and figures grey, warnings orange. For 5 s after the radio moves to another
cell, an arrow points to the new location in bright green, e.g.
`→ Z7 StClair W`, and the grey line
says where the radio came from, e.g. `from Z6 Downsview`. The green LED is
on while the radio is locked to a cell.

One call at a time: when several calls are active on the cell, the radio
plays and shows the first one, through the pauses between its
transmissions, until it has been silent for 2 s. Then another call can take
over. An alarm call takes over at once. Every call still goes to the recent
calls. A call also wakes the screen and keeps it on until it ends.

The meter at the bottom has three labelled bars: `VOL` the speaker volume
(thin, grey), `SIG` the signal strength (S0 to S9+20) and `DEC` the
decoding quality, full at 10% phase error or better and empty at 45%,
where decoding fails. `SIG` and `DEC` are green when good, yellow when
fair and red when poor: from S7 and S4 for the signal, up to 21% and 32%
phase error for decoding. A call stays on screen for 10 s after its
last activity unless its release is decoded. During a call to or from an
alarm talkgroup the screen turns red, and three beeps sound once.

The top bar shows the talkgroup filter, `TG All` in white or another filter
in yellow (section 6), or `Hold <name>` in yellow while a talkgroup is held:
a temporary filter that lets only its calls through. The buttons are in
the quick reference at the top of this file.

Speech starts 150 ms after the first traffic slot: TETRA leaves a gap of
113 ms in every multiframe (frame 18), which the buffer must cover. A
talking party shows as `unknown` (a called group as a dash) when the
network has not sent that identity since the radio started listening,
typically because the call was set up before.

The demodulator figures on line 1, e.g. `e11% -234Hz o0 r1`:

- `e`: RMS phase error in % of pi/4. About 10 to 20% on a clean signal;
  decoding fails above about 45%, where the demodulator starts again.
- the carrier offset in Hz.
- `o`: overruns, blocks of samples lost because the radio could not keep
  up. This should stay at 0: if it counts up, the radio is short of CPU
  time, and speech will break up.
- `r`: restarts of the burst synchronisation: at start, after an overrun,
  or after the demodulator followed a jump in the signal's timing. A
  replayed recording causes one at every loop.

How it works: the BK4819 runs the same flat, wide receive path as M17
(`REG_43 = 0x7e28`, AF bypass). The 48 kHz discriminator output goes to
`openrtx/src/protocols/TETRA/TetraDemod.c`: integrate-and-dump over each
symbol, a timing loop for the sample clock offset, a running fit of gain
and carrier offset, and a 9-tap equalizer seeded with taps measured on the
C62 (`CONFIG_TETRA_EQ_INIT` in `hwconfig.h`). Its soft bits go to libtetra.

Speech: clear (not encrypted) speech is played on the speaker. libtetra
decodes each traffic slot into two speech frames, and
[OpenACELP](https://github.com/sp5wwp/OpenACELP) decodes them into 8 kHz
audio, with its built-in TETRA codebooks (EN 300 395-2). The speaker is
released after one second without speech. `-DC62_TETRA_SPEECH=OFF` builds
without it.

The baseband dump keeps working in T mode, to capture what the radio
receives.

### Call following

A cell often has several carriers: the main one, which you tune to, carries
the control channel, and calls get traffic channels that may be on another
carrier. With call following the radio goes with the call: when a speech
call is given a channel on another carrier, it retunes there and decodes
only the call's timeslots, then comes back to the main carrier when the
call is released, stays silent for 10 s, turns out to be encrypted, or the
radio loses the signal. Line 3 of the call screen then shows the carrier
in small grey text after the location, e.g. `CN Tower 392.5375`.

- libtetra tracks the channel allocations and asks to retune. The radio
  decides which calls to follow, in
  `openrtx/include/protocols/TETRA/TetraFollow.h`.
- It follows one call at a time, the first clear speech call it sees,
  including calls that stay on the main carrier. Encrypted and data calls
  are not followed.
- All carriers of a cell share the TDMA timing, so the radio keeps its slot
  timing across the retune instead of synchronising again. A secondary
  carrier may carry no synchronisation bursts at all.
- The carrier frequencies come from the cell's broadcast, or from the
  allocation itself when it names another band or offset (extended carrier
  numbering). They keep your tuning offset from the main carrier. The radio
  does not follow if it is more than 12.5 kHz from the main carrier.
- With transmission trunking, where each transmission gets a new channel
  through the control channel, the radio comes back after the 10 s of
  silence and may miss the start of the next transmission.
- `-DC62_TETRA_FOLLOW=OFF` builds without it: the radio stays on the tuned
  carrier.

### Cell tracking

A TETRA network is many cells, each a base station with its own main
carrier: the surface sites, and the subway zones fed by radiating cable
along the tunnels. Each cell broadcasts the carriers of its neighbours. With
cell tracking the radio moves between cells by itself, like a TETRA radio
does:

- While locked, if the cell gets weak (below -95 dBm), it measures each
  neighbour every 3 s, retuning for about 5 ms, and moves to one that is at
  least 6 dB stronger twice in a row. It does not measure during a call.
- When the signal is lost for 2.5 s, it measures the neighbours and every
  TETRA channel of the channel list, then tries the strongest in turn,
  2.5 s each, until one locks. The screen shows `T scanning` meanwhile,
  and below it the channel being tried.
- The grey frequency shows the cell received. The talkgroup filter and the
  hold stay as they are.

A move costs up to a second of silence while the radio synchronises to the
new cell. The thresholds are first guesses: they need tuning on real
networks, with baseband dumps of moves that go wrong.
`-DC62_TETRA_ROAM=OFF` builds without it.

To check what the radio would do on a network without flashing, replay a
baseband dump through the same demodulator, libtetra and follower with
`tests/platform/tetra_trace.c` (build instructions in the file):

```bash
./tetra_trace tetra_burst001.wav
```

It prints the cell, every call control event with its channel allocation,
libtetra's retune requests with the radio's answer, and each retune the
radio would make (lines starting with `>>`). A recording holds a single
carrier, so it shows the decisions, not what the radio would hear
afterwards. Use `-n` for recordings not made with a C62.

## 6. Memory channels

The C62 build cannot store channels or settings yet: after power on it is
in VFO mode on 430.000 MHz, M17. A channel list can be built into the
firmware instead, read only:

```bash
docker run --rm -it --user "$(id -u):$(id -g)" -v "$PWD":/work openrtx-c62 \
    c62-build -DC62_CHANNELS=scripts/c62/channels/ttc_tetra.h
```

CMake remembers the option, so later builds keep the list. In VFO mode
press **ESC** for memory mode, then **Up** and **Down** to step through the
channels, and **ESC** again to return to VFO mode. The **Channels** entry
of the menu lists them too. Each channel sets its own mode, so a TETRA
channel starts T mode directly.

`scripts/c62/channels/ttc_tetra.h` holds the Toronto Transit Commission
TETRA network: the three carriers RadioReference lists as control channels
at each of its 10 surface sites, named after the site, then the control
channels of the 13 underground subway zones (`Z1 Kipling` to `Z13 VMC`).
Usually one carrier per surface site carries the control channel and
synchronisation: that is the one where the radio locks, and line 2 then
names the site, e.g. `T CN Tower`. The zones reuse four frequencies, so
underground the one you receive depends on where you are. Any of them is a
good start: cell tracking (section 5) moves the radio to the best cell and
follows it from zone to zone, and searches all these channels when the
signal is lost.

To make a list of your own, copy that file and keep one entry per line:

```c
CPS_CHANNEL("Name", 422662500, OPMODE_TETRA)
```

with the name (12 characters show on screen), the frequency in Hz and
`OPMODE_FM`, `OPMODE_M17` or `OPMODE_TETRA`. Channels are receive only.

### Talkgroups

The same file can name TETRA talkgroups and radios, and sort them into
filters you choose on the radio:

```c
TETRA_FILTER(SUBWAY, "Subway")
TETRA_FILTER(LINE1, "Line 1")

TETRA_ID(100010, "Line1 Line Calls", TF(SUBWAY) | TF(LINE1))
TETRA_ID(101011, "Transit Control", TF(CONTROL))
TETRA_ID(60002, "Red Alarm", TF_EVERY | TF_ALARM)
```

`TETRA_FILTER` defines a filter, in the order the radio steps through
them. `TETRA_ID` gives an SSI (the decimal ID), a name of up to 16
characters and the filters it belongs to: `TF()` of each, `TF_EVERY` for
all of them, `0` for none, plus `TF_ALARM` for an alarm talkgroup. The
call screen, last heard line, recent calls and short messages show the
names instead of numbers.

In T mode, **#** selects the next filter and **\*** the previous one. The
top bar shows it: `TG All` in white lets every call through, any other
filter is shown in yellow. A call gets through when its talkgroup, or for
a call between two radios either radio, is in the filter. Its voice, call
screen and call following go with it. The radio starts at `TG All` after
power on. A long **#** holds one talkgroup instead, see the quick
reference.

`ttc_tetra.h` names the 137 TTC talkgroups and radios RadioReference lists
and defines these filters:

| Filter | Talkgroups |
|---|---|
| Riding | What to hear on a train: the line calls of every line, Transit Control and subway incidents. One press of # from `TG All` |
| Subway | Rail operations on every line: line calls, PA, towers, yards, stations, train runs, subway incidents |
| Line 1, 2, 4, 5, 6 | That line's calls, PA, tower, yards and trains |
| Control | Transit Control, Events, Incidents 1 to 3 |
| Surface | All of the surface section |
| Streetcar | Streetcar control and supervisors, Surface Ch. 5, the carhouses |
| Bus | Bus operations, the garages, Wheel-Trans, Control to Bus PA |
| Maintenance | Signals, wayside, track, structures, radio techs, janitors, PRESTO, fire prevention |
| Security | Transit Enforcement, Special Constables |

`Red Alarm` and `911 Announce` come through every filter, and are alarms:
their calls turn the screen red and sound three beeps.

A range of identities can be named and filtered at once, for radios that
follow a numbering plan. Entries of their own still come first:

```c
TETRA_ID_RANGE(100201, 100299, "Run %u", 100000, TF(SUBWAY) | TF(LINE2))
```

names 100253 `Run 253`: the format gets the SSI minus the base. The TTC
list names every train run this way, from the pattern of the runs
RadioReference lists (100000 plus the run number, whose first digit is the
line). Line 4 runs are assumed to follow it.

Location areas can be named too, once you know them (the cell screen shows
`LA <n>` until then):

```c
TETRA_LA(39, "Eglinton east")
```

## 7. Preview the screen

`scripts/c62/ui_render.sh` draws the main screen on your computer with the
firmware's own UI code, at the C62's 160x128, and writes PNG files: TETRA
searching, locked, SDS, a call, a followed call and an encrypted call.
With `-c` it also draws memory mode for each channel of a list and uses
its talkgroup names; `-t` sets the group and talker of the example calls:

```bash
scripts/c62/ui_render.sh -c scripts/c62/channels/ttc_tetra.h -n 302-2013 \
    -t 101011-100112 out
```

It needs a C compiler and zlib, nothing from the radio. Radio state and
TETRA status are example values, set in `tests/platform/ui_render/ui_render.c`.
