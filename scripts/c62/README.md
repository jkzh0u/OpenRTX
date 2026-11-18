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
- reads a **channel list** you flash next to the firmware, with TETRA
  **talkgroup names** and **filters** chosen on the radio (section 6): one
  firmware for everyone, each with their own list. An example list and a
  full one for the Toronto Transit Commission network are included;
- **saves its settings** and the channel in use at power off;
- has a **recent calls** list with details, a **last heard** line, **talk
  time**, a talkgroup **hold**, **alarm** alerts, a decoding quality meter
  and colour in T mode, and plays one call at a time;
- can stream the raw receiver audio over the programming cable (section 3);
- builds with Docker (section 1), and its screens can be previewed on a
  computer (section 7).

It is receive only in T mode.

## Quick start

1. **Get an FT232R programming cable** (USB ID `0403:6001`) with a 2-pin
   Kenwood plug, and install
   [`cskburn`](https://github.com/LISTENAI/cskburn/releases) (section 2).
2. **Back up your radio's flash** before anything else (section 2). It holds
   calibration unique to your radio, and is the only way back to the stock
   firmware.
3. **Build the firmware** with Docker (section 1). It is the same for
   everyone.
4. **Write your channel list**: copy
   [`channels/example.h`](channels/example.h), put in your channels and
   talkgroups, and build its image with
   `python3 scripts/chlist.py my_channels.h -o channels.bin` (section 6).
   [`channels/ttc_tetra.h`](channels/ttc_tetra.h) is a full example for
   Toronto.
5. **Flash** the firmware, the DSP image and your list (section 2). For a
   first flash, follow [FIRST_FLASH.md](FIRST_FLASH.md), which turns the
   features on in stages.
6. **Watch the log** with `scripts/c62_log.py` over the same cable
   (section 5, Receive log) to see what the receiver does.

Later, a new list only needs step 4 and flashing `channels.bin`; settings
you change on the radio are kept across power off.

## Quick reference: buttons

The C62's keys have no ESC or Enter labels. This README uses OpenRTX's
names for them:

| On the radio | Name here |
|---|---|
| Green key, top left | **Enter** (also opens the menu) |
| Red key, top right | **ESC** (back, cancel) |
| Round key between them | **Up** / **Down** |
| **P1** / **P2** | F1 / F2 |
| Button on the side, below the PTT | **side key** |

This follows the order the keypad driver reads the keys in, and is not
yet confirmed on a radio: if a key does something else, the checklist in
FIRST_FLASH.md (stage 4) is where to note it.

On the main screen, in any mode:

| Button | Does |
|---|---|
| **Green** (Enter) | Open the menu. The **Channels** entry lists the channels of your channel list |
| **Red** (ESC) | Switch between VFO mode (a frequency) and memory mode (the channel list) |
| **Up** / **Down** (round key) | VFO: step the frequency. Memory: next / previous channel |
| **0** to **9** | VFO: type a frequency, then **Enter**. Memory: type a channel number, see below |
| Long **1** to **9** | Memory: the side key shortcut of that number, without the side key. Long **9** also unlocks the keypad |
| Long **5** | VFO: change mode, if 5 is the first digit typed (it is then dropped) |
| Side key + number | Shortcut, see below |

In memory mode a short press of a number types a channel number, shown
large in amber, and acts when you let go, so that holding the key does only
its shortcut. **Enter** jumps to the channel, **ESC** cancels, and after
2 s without a key, or at four digits, it jumps by itself. Any other key
drops the number. In T mode **0** on its own still opens the recent calls;
it is a digit once a number is started (for channel 10, 20...).

Shortcuts: hold the **side key** and press a number. Long-press the side key
on its own to latch shortcut mode, so the numbers work without holding it;
press it again to unlatch. In memory mode a long press of the number on its
own does the same, without the side key.

| Side key + | Does |
|---|---|
| **5** | Change mode: FM, M17, T (TETRA) |
| **1** / **2** / **3** | FM: CTCSS off, encode, decode or both / tone down / tone up |
| **4** | FM: bandwidth 12.5 or 25 kHz |
| **6** | Transmit power |
| **7** / **8** | Brightness down / up |
| **9** | Lock or unlock the keypad |
| **0** | Send the TETRA log kept by the radio over the cable (see 5, Receive log) |

In T mode, on the main screen:

| Button | Short press | Long press |
|---|---|---|
| **#** | Next talkgroup filter, or release a held talkgroup | Hold the current call's talkgroup, or the last one heard. Again to release |
| **\*** | Previous talkgroup filter, or release a held talkgroup | Line 1: switch between the last call heard and the signal figures |
| **0** | Recent calls | Search the known carriers for the best signal |

The recent calls list shows how long ago each call was on the right,
alarms in red and a lock before encrypted calls. **Up** / **Down** to
scroll, **Enter** to see the
selected call in detail (who talked last, how many times it was heard, when
it was first heard), **ESC** or **0** to go back. In the detail, **Enter**
holds that talkgroup and **ESC** goes back to the list.

A short press of **#**, **\*** or **0** acts when you let go of the key, so
that holding it does only the long press.

**Menu** > **Field test** shows what the receiver knows, like the service
screen of a TETRA radio. **Up** / **Down** to scroll, **Enter** to search
for the best signal (as long **0**), **ESC** to go back:

- the lock and phase error, signal in dBm, carrier offset, resyncs and
  samples lost (orange when any were lost);
- the cell: network, colour code, location area with its name, main
  carrier, load, air encryption;
- the frequency tuned, if not the main carrier, and a call's carrier while
  following it;
- what cell tracking is doing (on the cell, checking neighbours, measuring
  all, trying a carrier, waiting);
- the network's clock time;
- the neighbour cells the cell broadcasts, with their location areas;
- the strongest carriers of the last measurement by cell tracking, with
  their levels and how long ago; the cell received is green with a `*`.

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
| `OpenRTX/build_c62/zephyr/channels.bin`, with `-DC62_CHANNELS=` (section 6) | `0x2c0000` |

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
| `C62_BASEBAND_DUMP` | `OFF` | Send raw 48 kHz RX audio over the UART. The console log is muted after audio starts. Needs `C62_TETRA_LOG=OFF` (section 3) |
| `C62_BASEBAND_DUMP_BAUD` | `115200` | UART baud rate during the dump |
| `C62_BASEBAND_DUMP_BURST` | `ON` | Record 1 MiB (10.4 s) to PSRAM, send it, repeat |
| `C62_BASEBAND_DUMP_FORMAT` | `1` | `1` = 16-bit samples, `2` = 12-bit packed (13.9 s per burst) |
| `C62_TETRA` | `ON` | TETRA receive mode "T" (section 5), with libtetra |
| `C62_TETRA_SPEECH` | `ON` | Play clear TETRA speech, with OpenACELP (section 5) |
| `C62_TETRA_FOLLOW` | `ON` | Follow TETRA calls to the other carriers of a cell (section 5) |
| `C62_TETRA_ROAM` | `ON` | Move to the best TETRA cell as the radio moves, search when the signal is lost (section 5) |
| `C62_TETRA_LOG` | `ON` | TETRA receive log over the UART, and the last lines kept in PSRAM (section 5, Receive log) |
| `C62_TETRA_LOG_HISTORY` | `262144` | Bytes of log kept in PSRAM |
| `C62_CHANNELS` | empty | Also build the channel list image `channels.bin` from this list (section 6). The firmware itself holds no channels |
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

Flashing a new build for the first time: follow
[FIRST_FLASH.md](FIRST_FLASH.md), which turns the features on in stages.

Use [`cskburn`](https://github.com/LISTENAI/cskburn/releases) over the
Kenwood programming cable. To enter burn mode: radio off, hold the lower
side key, power on, release the key. The screen stays dark. Don't press the
main PTT while flashing: it shares the boot ROM's UART RX line. `cskburn`
resets the radio after each command, so enter burn mode again before each
one.

**The cable must use an FTDI FT232R chip (USB ID `0403:6001`).** "Genuine
FTDI" is not enough. A cable with an FT231X (`0403:6015`), such as the BTECH
PC03, has been reported to pass the boot ROM handshake and then fail every
flash operation with `ERROR [E5003]: Failed to load RAM burner`, while an
FT232R cable (DTECH K1) worked first time. That error looks like a software
problem, so check the cable's ID before debugging anything else:

```bash
system_profiler SPUSBDataType | grep -B4 "Vendor ID: 0x0403"  # macOS: Product ID: 0x6001
lsusb | grep 0403                                           # Linux: 0403:6001
```

On Windows: Device Manager, the port's Properties, Details, Hardware Ids
(`VID_0403&PID_6001`).

```bash
# Once, before anything else: back up your radio's own flash (it is per-unit)
cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --read 0x0:0x400000:backup.bin

cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --verify-all \
    0x100000 OpenRTX/build_c62/zephyr/dsp_firmware.bin \
    0x000000 OpenRTX/build_c62/zephyr/zephyr.bin

# Your channel list, once and whenever it changes (section 6)
cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --verify-all 0x2c0000 channels.bin
```

## 3. Capture the RX baseband

The dump is off by default, as it shares the cable with the TETRA receive
log: build with `-DC62_BASEBAND_DUMP=ON -DC62_TETRA_LOG=OFF` to use it.
A build folder made before keeps its own setting; if the build then stops
saying both use the console UART, add `-DC62_BASEBAND_DUMP=OFF` once.

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
- A long **0** searches for the best signal straight away, without
  waiting for the cell to get weak or the lock to be lost: it measures the
  neighbours, every TETRA channel of the channel list and the cell itself,
  then moves to the strongest, unless the cell received is within 3 dB of
  it. If the strongest does not lock, the next ones are tried, as when the
  lock is lost. The line under the station shows `scanning` meanwhile;
  measuring takes about 30 ms per carrier, and may click in a call.
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

### TETRA settings

**Menu** > **Settings** > **TETRA** switches parts of T mode on and off.
Select a line, press **green**, then **Up** turns it on and **Down** off;
**green** again or **red** to finish. Like the other settings they are
kept at power off, and all are on at first.

| Setting | On | Off |
|---|---|---|
| Follow calls | Follows a call to the carrier the network moves it to (Call following above) | Stays on the main carrier: calls on other carriers are shown but not heard |
| Cell tracking | Moves between cells by itself (Cell tracking above) | Stays on the channel you picked. A long **0** still searches once |
| Alarm beep | An alarm call beeps three times and turns the screen red | Red screen only |
| Keep filter | Starts with the talkgroup filter of the last power off | Starts at `TG All` |

Follow calls and Cell tracking only do something in a firmware built with
them, which is the default (`C62_TETRA_FOLLOW`, `C62_TETRA_ROAM`).

### Receive log

The radio writes a line for each thing the TETRA receiver decodes or
decides on the programming cable, at 115200 baud, so you can watch call
following and cell tracking on a real network. It also keeps the last
256 kB of lines, an hour or several depending on how busy the network is,
so you can ride with nothing connected and read them afterwards. A raw baseband dump needs bursts or a fast adapter;
the log is about 100 bytes a second at most.

```bash
pip install pyserial
python3 OpenRTX/scripts/c62_log.py -p /dev/ttyUSB0 -o ride.log
```

The script prints each line and appends it to the file, after the
computer's clock time. On the radio, **side key + 0** sends the kept lines,
between `--- stored log` and `--- end of stored log` (256 kB take about
25 s). Any serial terminal at 115200 baud works too, and so does a phone
with a USB OTG serial app.

Each line starts with the seconds since power on:

```
  12.345  sync locked
  12.890  cell 302-2013 LA 12 (Eglinton east) CC 5, main carrier 1234 = 392.51250
  13.100  network time 2026-11-16 08:12:33 local
  13.100  neighbours: 392.61250, 392.71250
  15.020  call 7 setup to G 101011 (Transit Control) from 100112 (Run 112), carrier 1236 = 392.56250 slots 2
  15.021  retune: call 7 to carrier 1236 slots 0x2 >> 392.56250
  15.100  voice call 7 from 100112 (Run 112)
  31.400  follow: back to the main carrier >> 392.51250
  40.100  roam: weak cell (-97 dBm), measuring 2 neighbours
  40.160  roam: measured 2: 392.61250 -88, 392.71250 -101
  46.200  roam: move to a stronger neighbour >> 392.61250 (-88 dBm)
  50.000  signal -85 dBm on 392.61250, locked, phase error 12%, offset -234 Hz
```

- `sync`, `cell`, `neighbours`, `network time`: the lock and the cell. A
  cell or neighbour line comes again only when it changes; the network
  time every 10 minutes, to match the log to a clock.
- `call`: every call control event, with the talkgroup and radio names of
  the channel list, the allocated carrier and timeslots, and `filtered
  out` for calls the talkgroup filter holds back. `voice call` marks the
  start of each transmission heard, `sds` a short message or status.
- `retune`: libtetra asking to follow a call to another carrier or back,
  and the radio's answer (`>>` where it tuned, or `declined` / `filtered
  out`). `follow:` is the radio going back on its own (silence,
  encryption, a filter change).
- `roam:`: cell tracking and the long **0** search: what was measured,
  the moves and tries, and `staying on` when the cell received is best.
- `filter`, `start on`, `samples lost`, and every 10 s `signal`: signal
  strength, frequency, lock, decoding quality.
- `--- N lines not sent` if the cable could not keep up; those lines are
  still in the kept log.

The kept lines are lost at power off. Zephyr and the DSP print on the same
line, mostly at boot. `-DC62_TETRA_LOG=OFF` builds without the log, and
`-DC62_TETRA_LOG_HISTORY=` sets how many bytes are kept.

## 6. Channels and settings

### Saved settings

The radio saves the menu settings (brightness, squelch, timers and so on)
and the channel in use when you turn it off with the knob, and starts with
them. The channel comes back in VFO mode: a memory channel you were on
returns as its frequency and mode, so T mode on a TETRA cell starts
receiving again by itself. The talkgroup filter comes back too, unless
Keep filter is off (TETRA settings); a held talkgroup does not.

The first start, or a firmware whose settings layout changed, uses the
defaults: VFO mode on 430.000 MHz, M17. They are stored with Zephyr's NVS
in the flash area at `0x2b0000`, which the stock firmware leaves empty; the
calibration at `0x3b0000` is never written. Settings are written only at
power off, and only what changed. Taking the battery out while on keeps the
last saved ones.

### Channel lists

Memory mode channels, and the names and filters of TETRA talkgroups, come
from a **channel list** flashed on its own at `0x2c0000`, next to the
firmware. The firmware holds none: everyone flashes the same firmware and
their own list, and changing the list does not need a new firmware.

A list is a text file, one entry per line. Start from
[`channels/example.h`](channels/example.h):

```c
CPS_CHANNEL("NOAA WX 1", 162550000, OPMODE_FM)
CPS_CHANNEL("TETRA Site 1", 390012500, OPMODE_TETRA)
```

with the name (ASCII, 12 characters show on screen), the frequency in Hz
and `OPMODE_FM`, `OPMODE_M17` or `OPMODE_TETRA`. Channels are receive only.

Turn the list into an image and check it, with Python 3 and nothing else:

```bash
python3 OpenRTX/scripts/chlist.py my_channels.h -o channels.bin
python3 OpenRTX/scripts/chlist.py --show channels.bin
```

The script stops with the line number of anything it cannot read. Then
flash the image, radio in burn mode (section 2):

```bash
cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --verify-all 0x2c0000 channels.bin
```

To flash it with the firmware, add `0x2c0000 channels.bin` to the firmware
command. A build with `-DC62_CHANNELS=my_channels.h` (relative to the
OpenRTX folder) also writes the image as `OpenRTX/build_c62/zephyr/
channels.bin`.

At power on the radio checks the image (a CRC over all of it) and prints
`Channel list: N channels, N TETRA names` on the log line. With no list, or
a damaged one, it has no memory channels and shows TETRA numbers instead
of names; everything else works. To remove a list, flash an empty one: an
image built from an empty file.

In VFO mode press **ESC** for memory mode, then **Up** and **Down**, or
type the channel number (quick reference), and **ESC** again to return to
VFO mode. The **Channels** entry of the menu lists them too. Each channel
sets its own mode, so a TETRA channel starts T mode directly.

### Talkgroups

The same list can name TETRA talkgroups and radios, and sort them into
filters you choose on the radio:

```c
TETRA_FILTER(OPS, "Operations")
TETRA_FILTER(MAINT, "Maintenance")

TETRA_ID(1001, "Dispatch", TF(OPS))
TETRA_ID(2001, "Maint Crew", TF(MAINT))
TETRA_ID(9999, "Emergency", TF_EVERY | TF_ALARM)
```

`TETRA_FILTER` defines a filter, in the order the radio steps through
them, up to 30. `TETRA_ID` gives an SSI (the decimal ID), a name of up to
16 characters and the filters it belongs to: `TF()` of each, `TF_EVERY` for
all of them, `0` for none, plus `TF_ALARM` for an alarm talkgroup, whose
calls turn the screen red and sound three beeps. The call screen, last
heard line, recent calls and short messages show the names instead of
numbers.

In T mode, **#** selects the next filter and **\*** the previous one. The
top bar shows it: `TG All` in white lets every call through, any other
filter is shown in yellow. A call gets through when its talkgroup, or for
a call between two radios either radio, is in the filter. Its voice, call
screen and call following go with it. A long **#** holds one talkgroup
instead, see the quick reference.

A range of identities can be named and filtered at once, for radios that
follow a numbering plan. Entries of their own still come first:

```c
TETRA_ID_RANGE(500001, 500999, "Unit %u", 500000, TF(OPS))
```

names 500123 `Unit 123`: the name has one `%u`, which gets the SSI minus the
base.

Location areas can be named too, once you know them (the cell screen shows
`LA <n>` until then):

```c
TETRA_LA(1, "Downtown")
```

### Example: Toronto Transit Commission

[`channels/ttc_tetra.h`](channels/ttc_tetra.h) is a full list for the TTC's
TETRA network (MCC 302, MNC 2013): the three carriers RadioReference lists
as control channels at each of its 10 surface sites, named after the site,
then the control channels of the 13 underground subway zones (`Z1 Kipling`
to `Z13 VMC`). Usually one carrier per surface site carries the control
channel and synchronisation: that is the one where the radio locks, and
line 2 then names the site, e.g. `T CN Tower`. The zones reuse four
frequencies, so underground the one you receive depends on where you are.
Any of them is a good start: cell tracking (section 5) moves the radio to
the best cell and follows it from zone to zone, and searches all these
channels when the signal is lost.

It names the 137 TTC talkgroups and radios RadioReference lists, every
train run (`Run 253`, from the pattern of the runs RadioReference lists:
100000 plus the run number, whose first digit is the line; Line 4 runs are
assumed to follow it), and defines these filters:

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

`Red Alarm` and `911 Announce` come through every filter, and are alarms.

## 7. Preview the screen

`scripts/c62/ui_render.sh` draws the main screen on your computer with the
firmware's own UI code, at the C62's 160x128, and writes PNG files: TETRA
searching, locked, a channel number being typed, the best signal search,
SDS, a call, a followed call and an encrypted call.
With `-c` it also draws memory mode for each channel of a list and uses
its talkgroup names; `-t` sets the group and talker of the example calls:

```bash
scripts/c62/ui_render.sh -c scripts/c62/channels/ttc_tetra.h -n 302-2013 \
    -t 101011-100112 out
```

It needs a C compiler and zlib, nothing from the radio. Radio state and
TETRA status are example values, set in `tests/platform/ui_render/ui_render.c`.
