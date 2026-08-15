# LiquidCam

A native C++ replacement for NZXT CAM, built on the same device protocols as
[liquidctl](https://github.com/liquidctl/liquidctl) and wrapped in a Qt widget
interface that takes inspiration from CAM's layout.

<img style="width: 80%; height: auto;" alt="image" src="https://github.com/user-attachments/assets/91f2b91f-4409-4a8a-95ba-e1879fa328c7" />

I made this as a way to get rid of NZXT CAM, which is wildly expensive on the CPU. The main goal was to get PSU logging so that my PSU didn't ramp up to 100% every hour or so.
LiquidCam sits between 8-12MB of RAM and 0% CPU while in the tray. When maximized, will sit between 0.1% and 0.5% CPU (tested on 7800X3D)

Built using Claude Opus 5 Max, with additional fixes by me to improve code quality/safety.

Targets exactly these hardware devices (version 1.0.0):

| Device | USB ID | What LiquidCam does |
| --- | --- | --- |
| NZXT Smart Device V1 | `1e71:1714` | Fan speed control on all three channels, fan RPM / voltage / current / control mode, noise level, LED accessory detection, all 15 firmware lighting presets |
| NZXT E Series PSU | `7793:2500` | Temperature, fan speed control, power-on hours, per-rail voltage / current / power, total output, firmware version (E500 `5911` and E650 `5912` are recognised too) |

Feel free to put in a request if this project looks useful to you, I'd love to add more device compatibility!

## Why it is cheap on CPU

NZXT CAM idles at a few percent of a core because it runs a Chromium process, polls
on timers, and animates. LiquidCam is built the other way around:

- **No third-party HID library.** The transport talks to `hid.dll` and
  `setupapi.dll` directly with overlapped I/O. A read parks in the kernel until
  a report arrives, so waiting costs nothing.
- **One worker thread owns every handle.** The GUI thread never issues a USB
  transfer; it posts a command and returns. Nothing can stall the interface.
- **Snapshots, not signals per value.** The worker fills a plain trivially
  copyable struct and publishes it under a mutex once per tick. One `updated()`
  signal per cycle, not one per reading.
- **Nothing repaints when nothing is visible.** Hiding the window drops the
  poll rate by the idle multiplier and stops the UI signal entirely. Fan curves
  keep running.
- **Writes are deduplicated.** A duty change under two points is dropped, and
  slider drags are coalesced into a single USB write with a 180 ms debounce.
- **Fixed-size buffers everywhere in the polling path.** No allocation happens
  between waking up and going back to sleep.

Expect the poll loop to sit near zero CPU at the default one second interval,
with the PSU swept every third tick.

## Building

Requirements: Windows 11, Visual Studio 2026 (or 2019/2022), and the Qt 5.14.2
`msvc2017_64` build installed.

1. Point the build at Qt. Either set an environment variable once:

   ```
   setx QTDIR C:\Qt\Qt5.14.2\5.14.2\msvc2017_64
   ```

   or edit the `QtDir` property near the top of `LiquidCam.vcxproj`. The build
   stops with a readable error if `moc.exe` is not found there.

2. Open `LiquidCam.sln`, pick **Release | x64**, and build. Or from a developer
   prompt:

   ```
   msbuild LiquidCam.sln /p:Configuration=Release /p:Platform=x64 /m
   ```

   `build.bat` does the same thing and finds MSBuild through `vswhere`.

The output lands in `build\x64\Release\LiquidCam.exe`, and the post-build step
runs `windeployqt` so the Qt DLLs sit next to it and the folder is portable.

### Notes on the toolchain

- **Platform toolset.** The project selects `v145` (Visual Studio 2026) when it
  is installed and falls back to whatever the current Visual Studio ships,
  so the same file builds unchanged in 2019 and 2022.
- **Linking Qt built with MSVC 2017.** MSVC keeps binary compatibility from
  v140 through v145, so the prebuilt Qt 5.14.2 libraries link against a v145
  build. Link with the newest toolset in the mix, which is what happens here.
- **Debug builds.** Debug is set up and works, but it links Qt's debug
  libraries, and mixed-toolset debug CRTs are the one combination Microsoft
  does not promise. Release is compiled with `/Zi` and full PDBs, so debug the
  optimised build if anything looks strange.
- **C++ standard.** `stdcpp17`. Qt 5.14 headers predate C++20's rewritten
  comparison operators; switch `LanguageStandard` to `stdcpp20` only if you are
  ready to chase the resulting ambiguity warnings.
- **No Qt VS Tools required.** `moc` runs as a custom build step and its output
  is pulled in with `#include "moc_<Name>.cpp"` at the bottom of the matching
  `.cpp`. There is no `.ui`, no `.qrc`, and no code generation you cannot see.
- **Three Qt modules, no more.** QtCore, QtGui and QtWidgets. Single-instance
  detection is a named Win32 mutex plus a registered broadcast message rather
  than `QLocalServer`, which keeps Qt5Network.dll and its plugins out of the
  deployed folder for the sake of about two Win32 calls.

### Qt 5 and the VS 2026 toolset: `'stdext': identifier not found`

Qt 5 hands `stdext::make_checked_array_iterator` to `std::copy` and
`std::equal` inside `QVector`, `QList` and `QVarLengthArray`. That was a
Microsoft extension, deprecated in VS 2019 and deleted from the headers in the
toolset shipped with Visual Studio 2026, so every file that instantiates one of
those containers fails with a pile of C2065 / C3861 / C2039 errors naming
`stdext`. It is a Qt-versus-toolset problem, not a problem with any code here,
and Qt 5.14.2 will not be patched for it — Qt fixed it in Qt 6 by gating the
MSVC spelling behind a compiler version check.

`src/MsvcCompat.h` handles it. The project force-includes it into every
translation unit; it pulls in `qglobal.h` and then rewrites
`QT_MAKE_CHECKED_ARRAY_ITERATOR` and `QT_MAKE_UNCHECKED_ARRAY_ITERATOR` to the
plain `(x)` form Qt already uses on Linux, macOS and Qt 6. Predefining the
macros on the command line does not work, because Qt's own `#define` is not
guarded by `#ifndef` and simply overwrites them, which is why the override has
to come after `qglobal.h`.

Nothing about the generated code changes: the checked iterators only added
bounds assertions to debug builds, and the pointer arithmetic Qt performs is
the same either way.

If you would rather sidestep the whole thing, install the **MSVC v143 build
tools** as an individual component in the Visual Studio Installer and set
`PlatformToolset` to `v143` — that toolset still ships `stdext`. The compat
header is the better answer; it keeps you on the current compiler.

## Using it

- **Lighting.** Pick an effect, set the colours, drag brightness. Changes go to
  the device as you make them and are written to the settings file a moment
  later. "Restore this lighting when LiquidCam starts" replays the preset at
  launch, which is what makes this a real CAM replacement: the Smart Device
  forgets its lighting on every power cycle.
- **Cooling.** Each channel is Fixed, Silent, Performance, or a custom curve.
  Drag a point to move it, double-click empty canvas to add one, right-click a
  point to remove it. The green marker shows where the current sensor reading
  lands.
- **Power.** Live rail telemetry from the E850. Monitoring only.
- **Preferences.** Startup behaviour, curve input, poll rates, and an activity
  log.

Settings live in `%APPDATA%\LiquidCam\LiquidCam.ini`. "Start LiquidCam when I
sign in" writes `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` with a
`--minimized` argument; nothing is installed as a service.

### Curve inputs

The Smart Device has no temperature sensor, so curves need a source from
elsewhere. LiquidCam reads the ACPI thermal zone through WMI
(`MSAcpi_ThermalZoneTemperature`), which many boards do not publish. If the CPU
readout stays blank, switch the curve input to CPU load, which is derived from
`GetSystemTimes` and always works. `TempMonitor` is a small class with one job,
so wiring in LibreHardwareMonitor later is a contained change.

## Before you run it

- **Close CAM first.** Two programs writing to the same HID endpoint will fight
  over the fan and lighting state.
- **Initialisation matters.** The Smart Device only reports fan data after it
  has been told to detect and stream. LiquidCam does that at startup, and again
  automatically when the device goes quiet after a resume from sleep.
- **The protocol is reverse-engineered.** It is faithful to liquidctl, which is
  well tested on this hardware, but NZXT publishes nothing. Read
  `docs/PROTOCOL_NOTES.md` before changing any byte offsets.

## Layout

```
LiquidCam.sln / .vcxproj / .filters / .user
src/
  main.cpp                 entry point, single instance, tray startup
  MsvcCompat.h             forced include, see the toolchain notes above
  core/
    HidDevice.{h,cpp}      Win32 HID transport, overlapped reads
    SmartDeviceV1.{h,cpp}  fan control, lighting, status parsing
    SeasonicEPsu.{h,cpp}   PMBus over the vendor HID wrapper
    Pmbus.h                command codes, LINEAR11 / ULINEAR16
    DeviceManager.{h,cpp}  worker thread, command queue, curve driving
    TempMonitor.{h,cpp}    WMI thermal zone plus CPU load
    FanCurve.h             interpolation and presets
    Types.h                shared POD types and the LED mode table
  app/Settings.{h,cpp}     INI persistence, Run key
  ui/
    MainWindow.{h,cpp}     navigation and the four pages
    FanCurveWidget.{h,cpp} the curve editor
    Theme.{h,cpp}          stylesheet and the generated app icon
  app.rc / app.ico
docs/PROTOCOL_NOTES.md
```

## Licence

The device protocols were derived from liquidctl, which is GPL-3.0-or-later, so
this project carries the same licence.
