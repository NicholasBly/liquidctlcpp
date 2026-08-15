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

### Not available

**Power-on hours.** PMBus has no standard command for it, and liquidctl's
driver exposes nothing of the sort: its whole status list is temperature,
fan speed, firmware, and the five rails. If the controller keeps an hour
meter it would be behind one of the manufacturer-specific codes
(`0xd0`-`0xff`), none of which have been mapped for this family. Reads of
those codes are harmless, so a sweep of that range is the way to find out;
writes to them are not, and should not be attempted casually.

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
