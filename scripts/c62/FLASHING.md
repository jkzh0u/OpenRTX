<!--
 - SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 -
 - SPDX-License-Identifier: GPL-3.0-or-later
-->

# Flashing the C62

Step by step, for macOS, Linux and Windows. It takes about 15 minutes the
first time.

This firmware has not been run on a C62 yet. For a first flash, follow
this page to set up and make the backup, then
[FIRST_FLASH.md](FIRST_FLASH.md), which turns the features on in stages.

## What you need

- **A USB programming cable with a 2-pin Kenwood plug** (the kind sold for
  Baofeng and Retevis radios). Which cables work with the C62 is not known
  yet: if yours does or does not, note its make and chip.
- **The firmware files.** Build them with Docker (README section 1). You
  get, in `OpenRTX/build_c62/zephyr/`:

  | File | What it is | Flash at |
  |---|---|---|
  | `zephyr.bin` | The firmware | `0x000000` |
  | `dsp_firmware.bin` | The DSP core's firmware (downloaded by the build) | `0x100000` |
  | `channels.bin` | Your channel list, with `-DC62_CHANNELS=` (README section 6) | `0x2c0000` |

- **`cskburn`**, the flashing tool for the radio's chip. Step 1 installs it.
- About **5 MB of space** for the backup, and somewhere to keep a copy of
  it other than your computer.

## 1. Install cskburn

### macOS

With [Homebrew](https://brew.sh):

```bash
brew install listenai/tap/cskburn
cskburn --version
```

Without Homebrew: download `cskburn-darwin-arm64.tar.xz` (Apple silicon)
or `cskburn-darwin-x64.tar.xz` (Intel) from the
[releases](https://github.com/LISTENAI/cskburn/releases), then:

```bash
cd ~/Downloads
tar -xf cskburn-darwin-arm64.tar.xz
xattr -d com.apple.quarantine cskburn   # else macOS refuses to run it
sudo mv cskburn /usr/local/bin/
cskburn --version
```

### Linux

Download `cskburn-linux-x64.tar.xz` from the
[releases](https://github.com/LISTENAI/cskburn/releases)
(`cskburn-linux-arm64.tar.xz` on a Raspberry Pi 4 or 5 with a 64-bit
system), then:

```bash
cd ~/Downloads
tar -xf cskburn-linux-x64.tar.xz
sudo install cskburn /usr/local/bin/
cskburn --version
```

Let your user open serial ports, then **log out and back in**:

```bash
sudo usermod -aG dialout "$USER"   # Debian, Ubuntu, Fedora
sudo usermod -aG uucp "$USER"      # Arch
```

### Windows

1. Download `cskburn-win32-x64.zip` from the
   [releases](https://github.com/LISTENAI/cskburn/releases).
2. Make a folder `C:\c62` and unzip `cskburn.exe` into it.
3. Open **PowerShell**, then:

   ```powershell
   cd C:\c62
   .\cskburn.exe --version
   ```

The commands below are run from `C:\c62`, with the firmware files copied
there (step 3).

## 2. Find the cable's port

Plug the cable into the computer, **not into the radio yet**.

### macOS

```bash
ls /dev/cu.*
```

Run it once without the cable and once with it: the new name is the
cable, such as `/dev/cu.usbserial-A10KXYZ` or `/dev/cu.wchusbserial1410`.
Use the `cu.` name, not the `tty.` one.

### Linux

```bash
ls /dev/ttyUSB* /dev/ttyACM*
```

Usually `/dev/ttyUSB0`. If nothing appears, `sudo dmesg | tail` shows
what happened when the cable was plugged in. On Ubuntu, if the cable is
found and then lost again, the `brltty` package (braille displays) took
it: `sudo apt remove brltty` and plug it in again.

### Windows

Open **Device Manager** (right-click the Start button) and expand **Ports
(COM & LPT)**. The cable shows as `USB Serial Port (COM3)` or similar:
note the `COM` number. If it shows under **Other devices** with a warning
sign, its driver is missing: the cable seller's page, or the chip maker
(FTDI, WCH for CH340, Silicon Labs for CP210x, Prolific), has it.

**From here on, use your port name** where the commands say `PORT`.

## 3. Put the files in one place

### macOS and Linux

Nothing to do: the commands run from the workspace folder (the one that
holds `OpenRTX`) and read the files where the build left them.

### Windows

Docker builds the firmware inside WSL, but flashing is easier from
Windows itself, which sees the cable without extra setup. Copy the files
to `C:\c62`, from the WSL shell in the workspace folder:

```bash
cp OpenRTX/build_c62/zephyr/{zephyr,dsp_firmware,channels}.bin /mnt/c/c62/
```

(leave out `channels` if you did not build a list).

## 4. Enter burn mode

Every `cskburn` command needs the radio in burn mode, and the radio leaves
it after each command, so you do this before **each** command:

1. Turn the radio **off** with the knob.
2. Plug the cable into the radio's Kenwood jack, all the way in.
3. **Hold the lower side key.**
4. Turn the radio **on**, keep holding for a second, then let go.
5. The screen stays **dark**. That is burn mode.

Never press the **PTT** while the cable is in: it shares a line with the
chip's boot loader and breaks the transfer.

## 5. Back up the radio

Do this before flashing anything. The radio's flash holds calibration
made for this unit only, and the backup is the only way back to the stock
firmware. It reads the whole 4 MB, which takes a few minutes.

macOS and Linux:

```bash
cskburn -s PORT -C 6 -b 115200 -v --read 0x0:0x400000:backup.bin
```

Windows:

```powershell
.\cskburn.exe -s PORT -C 6 -b 115200 -v --read 0x0:0x400000:backup.bin
```

When it ends, `backup.bin` should be exactly 4,194,304 bytes:

```bash
ls -l backup.bin                          # macOS, Linux
```

```powershell
(Get-Item backup.bin).Length              # Windows
```

**Copy `backup.bin` somewhere else now**: a USB stick, cloud storage, an
email to yourself. Keep it for as long as you own the radio.

## 6. Flash the firmware

Burn mode again (step 4), then:

macOS and Linux:

```bash
cskburn -s PORT -C 6 -b 115200 -v --verify-all \
    0x100000 OpenRTX/build_c62/zephyr/dsp_firmware.bin \
    0x2c0000 OpenRTX/build_c62/zephyr/channels.bin \
    0x000000 OpenRTX/build_c62/zephyr/zephyr.bin
```

Windows:

```powershell
.\cskburn.exe -s PORT -C 6 -b 115200 -v --verify-all `
    0x100000 dsp_firmware.bin `
    0x2c0000 channels.bin `
    0x000000 zephyr.bin
```

Leave out the `0x2c0000` line if you have no channel list. `--verify-all`
reads everything back and stops with an error if any of it differs. At the
end the radio restarts into the new firmware.

**Later updates:** a new firmware only needs the `0x000000` line, a new
channel list only the `0x2c0000` line. The DSP firmware only changes when
the build says it downloaded a new one.

## 7. Check that it runs

The firmware writes a log on the same cable at 115200 baud. With Python 3
and `pip install pyserial`:

```bash
python3 OpenRTX/scripts/c62_log.py -p PORT -o first.log        # macOS, Linux
```

```powershell
python OpenRTX\scripts\c62_log.py -p PORT -o first.log         # Windows
```

(on Windows, copy `c62_log.py` next to the other files if the OpenRTX
folder is only inside WSL, and run it as `python c62_log.py ...`).

Turn the radio off and on without holding the side key. You should see
the startup lines, `Channel list: N channels, N TETRA names` if you
flashed one, and `TETRA log, 256 kB kept`. The screen shows the main
screen. Then carry on with [FIRST_FLASH.md](FIRST_FLASH.md).

## Going back to the stock firmware

Burn mode, then flash the backup over the whole flash. This also removes
the channel list and the saved settings.

```bash
cskburn -s PORT -C 6 -b 115200 -v --verify-all 0x000000 backup.bin
```

(`.\cskburn.exe` on Windows).

## When something goes wrong

| What you see | Try |
|---|---|
| `cskburn` waits, then times out | The radio is not in burn mode or the plug is not all the way in. Turn it off and do step 4 again. Press the plug in firmly: Kenwood plugs often need a push past a first click |
| Permission denied on the port (Linux) | The `dialout` / `uucp` step, then log out and back in |
| The port is busy | Something else has it open: close `c62_log.py`, serial terminals, CPS software |
| macOS says cskburn "cannot be opened" | `xattr -d com.apple.quarantine /usr/local/bin/cskburn`, or install with Homebrew |
| No COM port on Windows | The cable's driver (step 2), or try another USB port |
| It connects, then fails before writing | Note the exact error and the cable's make and chip, and try once more. Another cable may be needed |
| `--verify-all` reports a mismatch | Flash again. If it repeats at the same address, stop and keep the output |
| The radio does not start after flashing | Flash again. If it still does not start, flash `backup.bin` to get the stock firmware back |

Keep the full output of any failed command: run it again with `2>&1 | tee
flash.log` on macOS and Linux, or `*> flash.log` in PowerShell.
