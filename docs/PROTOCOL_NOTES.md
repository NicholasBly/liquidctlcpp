# Protocol notes

Everything here is reverse-engineered. The Smart Device V1 encoding is taken
from liquidctl's `smart_device.py` (`SmartDevice`), the power supply from
`seasonic.py` (`SeasonicEDriver`). Both are widely used on this exact hardware,
which is the strongest evidence available, but NZXT documents none of it.

Read this before you touch a byte offset.

## Report sizes and the report-ID byte

Windows includes the report-ID byte in `InputReportByteLength` and
`OutputReportByteLength`, and liquidctl's message arrays also start with that
byte. The two line up, so offsets can be copied across unchanged.

`HidDevice::writeMessage` builds a buffer of exactly `OutputReportByteLength`:

- message length equals the report length, so byte 0 is the report ID and the
  message is copied as-is (Smart Device: 65 bytes starting `0x02`; PSU: 64
  bytes starting `0xad`);
- message is one byte shorter than the report, so a `0x00` placeholder is
  prepended, which is what unnumbered-report devices need.

`SeasonicEPsu::transact` also tolerates a leading `0x00` on the way back, in
case the PSU interface turns out to be unnumbered on some firmware.

## NZXT Smart Device V1 (`1e71:1714`)

Output reports: 65 bytes. Input reports: 21 bytes.

### Initialisation

| Message | Effect |
| --- | --- |
| `01 5c` | Detect connected fans and LED accessories |
| `01 5d` | Start streaming status reports |

The first report after initialisation carries the static information:

| Offset | Meaning |
| --- | --- |
| `0x0b`..`0x0e` | Firmware version, four components. CAM shows `[0x0b].[0x0e]` |
| `0x10 >> 3` | LED accessory type: 0 = HUE+ strip (10 LEDs), 1 = Aer RGB fan (8 LEDs) |
| `0x11` | Number of LED accessories |

Reporting stops when the device loses power. If three consecutive polls read
nothing, LiquidCam re-runs initialisation and reapplies the saved lighting,
which is what recovers the device after a resume from sleep.

### Status reports (21 bytes, one per fan channel)

| Offset | Meaning |
| --- | --- |
| `1` | Noise level, dB, from the onboard microphone |
| `3`,`4` | Fan speed, big endian, rpm |
| `7`,`8` | Voltage: whole volts, hundredths |
| `9`,`10` | Current: whole amps, hundredths |
| `15 >> 4` | Channel index |
| `15 & 0x03` | 0 = nothing connected, 1 = DC, 2 = PWM |

### Fan speed

```
02 4d <channel 0-2> 00 <duty 0-100>
```

### Lighting

Two packets per animation step, colours in **GRB**, always 40 LEDs' worth of
data whether or not that many are attached:

```
packet 1: 02 4b <mode> <variant> <byte4> <colour bytes 0..56>
packet 2: 03 <colour bytes 57..119>
```

`byte4 = animation speed | (step index << 5) | size`, where speed runs 0
(slowest) to 4 (fastest). Bit `0x10` of `<variant>` reverses the direction.

| Effect | mode | variant | size | colours |
| --- | --- | --- | --- | --- |
| Off | `0x00` | `0x00` | `0x00` | 0 |
| Fixed | `0x00` | `0x00` | `0x00` | 1 |
| Fading | `0x01` | `0x00` | `0x00` | 1-8 |
| Spectrum wave | `0x02` | `0x00` | `0x00` | 0 |
| Marquee 3/4/5/6 | `0x03` | `0x00` | `0x00`/`0x08`/`0x10`/`0x18` | 1 |
| Covering marquee | `0x04` | `0x00` | `0x00` | 1-8 |
| Alternating | `0x05` | `0x00` | `0x00` | 2 |
| Moving alternating | `0x05` | `0x08` | `0x00` | 2 |
| Pulse | `0x06` | `0x00` | `0x00` | 1-8 |
| Breathing | `0x07` | `0x00` | `0x00` | 1-8 |
| Candle | `0x09` | `0x00` | `0x00` | 1 |
| Wings | `0x0c` | `0x00` | `0x00` | 1 |

Multi-colour effects send one step per colour, each with its own packet pair
and an incremented step index.

There is no brightness register. `LightingConfig::brightness` scales the RGB
values in software before they are converted to GRB, which is how CAM's
brightness slider behaves anyway.

The per-LED "super" variants (`super-fixed`, `super-breathing`, `super-wave`)
use the same commands with 40 individual colours instead of one repeated
colour. The transport supports it; the UI does not expose a per-LED editor yet.

## NZXT E-series power supply (`7793:2500` for the E850)

Reverse-engineered in liquidctl's `nzxt_epsu.py` (class `NzxtEPsu`, formerly
`SeasonicEDriver`). PMBus wrapped in a Seasonic HID transport built on a
PIC16F1455. **The interface does not use numbered reports**: liquidctl builds a
1 + 64 byte packet with a zero in front and the payload shifted one to the
right, and Windows needs the same. `HidDevice::setUsesReportIds(false)` selects
that. The Smart Device is the opposite case, so length alone cannot decide it.
64-byte reports both ways, and the
controller drops commands sent closer together than about 2.5 ms, so
`SeasonicEPsu::pace()` enforces that gap.

### Request

```
ad <write length> <read length> <command length> 60 <command bytes...>
```

`0x60` is the PMBus address of the controller. Simple read:

```
ad 00 <n+1> 01 60 <command>   ->  aa <n+1> <n data bytes> <trailing byte>
```

Rail read, using `PAGE_PLUS_READ` (`0x06`) to select the rail:

```
ad 00 <n+2> 04 60 06 02 <page> <command>
     ->  aa <n+2> <n> <n data bytes>
```

A third byte of `0xff` in the response means the controller was busy. Every
transaction is retried up to three times.

**The input queue must be flushed before every request.** The controller
answers everything it is asked, so any reply that was not collected - a read
that timed out, or an attempt whose reply failed its length check - stays
queued and is handed to the *next* read instead. One orphan shifts the whole
sweep by a transaction: temperature returns the fan speed, the fan returns
something else, and every rail read fails validation. Retries make it worse,
because each one adds another orphan. `SeasonicEPsu::transact` calls
`HidD_FlushQueue` before writing for this reason.

Decoded values are range-checked before they are published (temperature
-40 to 150 C, fan 0 to 6000 rpm, rails 0 to 30 V). A decode outside those
bounds means the bytes were not what we thought they were, and the head of
the offending reply is put in the error text so it reaches the activity log.

### Values read

| Quantity | Command | Encoding |
| --- | --- | --- |
| Temperature | `READ_TEMPERATURE_2` `0x8e` | LINEAR11 |
| Fan speed | `READ_FAN_SPEED_1` `0x90` | LINEAR11 |
| Firmware | `MFR_SPECIFIC_FC` `0xfc` | see below |
| Rail voltage | `VOUT_MODE` `0x20` then `READ_VOUT` `0x8b` | ULINEAR16, exponent from `VOUT_MODE & 0x1f` |
| Rail current | `READ_IOUT` `0x8c` | LINEAR11 |
| Rail power | `READ_POUT` `0x96` | LINEAR11 |

Pages 0-4 are `+12V` peripherals, `+12V` EPS/ATX12V, `+12V` motherboard/PCIe,
`+5V` combined, `+3.3V` combined. Total output is the sum of the five rail
powers, matching what liquidctl reports.

The firmware read returns two bytes: minor, then major. Major is an **ASCII
character**, not a number, and the human string is `<major><minor as three
digits>` - `0x41, 17` gives `A017`. The number CAM shows is that same string
read back as hexadecimal, so `A017` becomes 40983. Treating the pair as a
plain little-endian 16-bit value looks right on paper and is wrong.

## What a capture of CAM shows

A 45 second recording of CAM already running, decoded, contains reads and
nothing else. Once per second CAM sends exactly this set:

| Request | Meaning | Sample reply |
| --- | --- | --- |
| `ad 00 02 01 60 8e` | READ_TEMPERATURE_2 | `aa 02 23 00` -> 35.0 C |
| `ad 00 02 01 60 90` | READ_FAN_SPEED_1 | `aa 02 fc 03 00` -> 1020 rpm |
| `ad 00 04 04 60 06 02 <page> 8b` | READ_VOUT, pages 0-4 | `aa 04 02 f7 05 db 00` |
| `ad 00 04 04 60 06 02 <page> 8c` | READ_IOUT, pages 0-4 | |
| `ad 00 04 04 60 06 02 <page> 96` | READ_POUT, pages 0-4 | `aa 04 02 ec da ce 00` -> 23.4 W |
| `ad 00 01 01 60 d2` | MFR 0xd2 | `aa 01 00` |
| `ad 00 04 03 60 d3 01 01` | MFR 0xd3 index 1 | `aa 04 02 00 1e 62 00` |
| `ad 00 04 03 60 d3 01 02` | MFR 0xd3 index 2 | `aa 04 02 00 2d 5d 00` |

Three things follow.

**The simple-read length is `dataLen`, not `dataLen + 1`.** CAM asks for two
bytes and gets `aa 02 ...`. liquidctl sends one more; both work, but there is no
reason to differ from the vendor.

**The 0xd3 pair is unidentified.** Both held steady across the whole capture:
`00 1e` and `00 2d`. Big-endian that is 30 and 45, little-endian 7680 and 11520.
An hour meter would not have a zero low byte in both samples, so these are more
likely the per-rail OCP limits CAM exposes for the CPU and GPU rails. LiquidCam
reads and reports them without claiming to know which.

**No writes at all**, across a window in which the fan mode was switched from
Performance to Silent to Fixed in CAM's interface. So whatever changes the fan
is not part of the steady-state loop. The fan spins up when CAM *connects*,
which is the window that needs recording - and the first version of the capture
script could not have seen it anyway, because it filtered frames by payload
prefix and so kept only commands that were already known.

## Capturing what CAM sends

`tools/Capture-PsuTraffic.ps1` records the conversation and decodes it. Run it
elevated with CAM open; it finds the right USBPcap interface by content rather
than by device address, since every request to this controller starts with
`0xad` and every reply with `0xaa`. Missing `USBPcap1` is normal - the numbers
follow root hubs and start wherever they start.

Two things make the output readable. Requests carry the write length in byte 1,
and every read liquidctl performs puts a zero there, so **any frame with a
non-zero byte 1 is a write** - that is where fan control lives. And CAM displays
uptime, so the capture necessarily contains the command that reads it; there is
no need to guess at manufacturer-specific codes.

Pausing between actions in CAM matters more than capture length. The gaps are
what tie a frame to the thing that caused it.

## Why writing 0x3b is defensible

Worth setting out, because PMBus does contain registers that can do real harm.

Dangerous by category, none of which this driver can reach:

| Register | Risk |
| --- | --- |
| `OPERATION` 0x01 | switches a rail off; instant power loss |
| `VOUT_COMMAND` 0x21 | changes an output voltage setpoint |
| `VOUT_OV_FAULT_LIMIT` 0x40, `IOUT_OC_FAULT_LIMIT` 0x46, `OT_FAULT_LIMIT` 0x4f | move overvoltage, overcurrent and overtemperature trip points |
| `STORE_DEFAULT_ALL` 0x11, `STORE_USER_ALL` 0x15 | commit settings to non-volatile memory, where a bad value survives a power cycle |

The last row is the one that actually deserves the word "destroy". Everything
else is recoverable by cutting power; a bad value written to NVM is not.

What makes `FAN_COMMAND_1` different:

- **It is not inferred.** NZXT's own software was captured writing
  `ac 04 60 3b 26 00 77` to this exact model, once per second. LiquidCam emits
  the identical frame. This is replay, not a guess at an unmapped register.
- **It is volatile, demonstrably.** The controller returns to its own fan curve
  within seconds of the host going quiet - the behaviour that started this
  whole investigation. Nothing written here can outlive the process.
- **The value is a duty percent, confirmed against reality.** 38 produces about
  960 rpm on this unit, matching what CAM displays. It is not an RPM field
  being misread.
- **A malformed frame fails closed.** The PEC is checked by the controller, so
  a corrupted write is rejected rather than misapplied.

Enforced in code rather than by convention: `execWrite` refuses any command
other than `FAN_COMMAND_1`, duty is clamped to 20-100 so the fan is never
commanded to stop, and above 60 C the requested duty is discarded in favour of
100% until the unit drops back under 52 C. The power supply's own
over-temperature shutdown is untouched and remains the real backstop.

The residual uncertainty is firmware behaviour nobody outside Seasonic can rule
out. It is the same uncertainty you accept every time CAM is open.

## Power-on time (solved)

Lifetime run time in **minutes**, 32 bit little-endian, at bytes 43 to 46 of
each `ae` chunk reply - past the reply's own declared byte count, not inside the
0xdc block, which is almost entirely zeros.

Read it the way CAM does: `af 00 ef 01 60 dc` opens the block, then `ae 01`
through `ae 06` walk it. The counter is present in every chunk; the first is
enough, but walking all six keeps the controller in the state it expects.

Confirmed against CAM. Two captures a day apart read 2,179,180 and 2,179,878,
and CAM displayed 1513D 20H shortly after the second. That is 36,332 hours, or
2,179,920 minutes: 59.999 counter units per hour, and 42 minutes past the second
capture. The gap between the captures, 698 minutes, is run time rather than
wall-clock time, which is why it is shorter than the day that elapsed.

The two `0xd3` registers are *not* this. They read 30 and 45 and never move,
which fits the per-rail OCP limits CAM exposes for the CPU and GPU rails.
LiquidCam still reports them once, labelled as unidentified.

## Fan control (solved)

Captured from CAM connecting. Two things were invisible before: writes use a
different opcode from reads, and the first version of the capture script kept
only frames beginning `0xad` or `0xaa`, so it discarded every one of them.

CAM's connect sequence:

```
b0 01                       announce the host, sent once
af 00 ef 01 60 dc           start a 239 byte block read of MFR command 0xdc
ae 01 .. ae 06              fetch it in six chunks
ac 04 60 3b 26 00 77        FAN_COMMAND_1 = 38, then repeated every second
```

The write frame is `[0xac][count][0x60][command][data...][pec]`, where the count
covers everything after the address. The PEC is a standard PMBus packet error
code: CRC-8, polynomial `x^8 + x^2 + x + 1`, taken over the address byte
`0xc0` (0x60 shifted left, write bit clear) followed by the command and data.
`3b 26 00` at address 0x60 gives `0x77`, matching the capture exactly.

The value is a duty percentage in a little-endian 16-bit field. CAM sits at 38,
which on this unit is roughly 960 rpm.

**It is a keepalive, not a setting.** CAM re-sends it every second, and the
controller returns the fan to its own curve when the host goes quiet. That is
the whole explanation for the fan slowing down with CAM closed, and it makes
stopping safe by construction: doing nothing hands cooling back to the unit.
LiquidCam therefore re-sends on every tick and suppresses the idle slowdown
while it is driving.

`FAN_COMMAND_1` is the only register LiquidCam ever writes, and the duty is
clamped to 20-100 so the fan is never commanded to stop.

### Not available

**Fan control.** CAM can change the E-series fan behaviour, but the command for it
is not in liquidctl and guessing at PSU firmware writes is a poor trade. The
PSU runs its own curve, which is the same thing that happens with CAM closed.

## Verified and unverified

Verified against liquidctl's implementation and its published sample output:

- Smart Device initialisation, status offsets, fan command, lighting packet
  layout and mode table, GRB ordering, step indexing.
- PSU request framing, response validation, LINEAR11 and ULINEAR16 decoding,
  rail pages, and the firmware string format.

Worth a second look on real hardware:

- **Report-ID handling.** The heuristic above covers both cases, but the first
  thing to check if a device is found and then answers nothing is whether the
  message needs the `0x00` prefix.
- **Temperature sensor.** Settled: sensor 1 returns something that is not a
  temperature on these units (a real E850 answered `0x03c0`, decoding to 960).
  liquidctl reads sensor 2 and so does LiquidCam.
- **Product IDs.** The E850 is `0x2500`. `0x5911` and `0x5912` for the E500 and
  E650 come from liquidctl's table and are listed for completeness.

If something does not respond, run `liquidctl --debug status` on the same
machine and compare. The byte layouts here are meant to line up exactly.
