// LiquidCam - Settings.cpp
#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace lc {
namespace settings {
namespace {

constexpr const char* kRunKey =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const char* kRunValue = "LiquidCam";

QString colorToHex(Rgb c)
{
    return QString::asprintf("%02X%02X%02X", c.r, c.g, c.b);
}

Rgb hexToColor(const QString& hex)
{
    bool ok = false;
    const uint value = hex.toUInt(&ok, 16);
    if (!ok)
        return Rgb{ 0x8B, 0x5C, 0xF6 };
    return Rgb{ static_cast<uint8_t>((value >> 16) & 0xFF),
                static_cast<uint8_t>((value >> 8) & 0xFF),
                static_cast<uint8_t>(value & 0xFF) };
}

QString curveToString(const FanCurve& c)
{
    QStringList parts;
    parts.reserve(c.count);
    for (uint8_t i = 0; i < c.count; ++i)
        parts << QStringLiteral("%1:%2").arg(c.points[i].x).arg(c.points[i].y);
    return parts.join(QLatin1Char(','));
}

FanCurve curveFromString(const QString& text)
{
    FanCurve c;
    const QStringList parts = text.split(QLatin1Char(','), QString::SkipEmptyParts);
    for (const QString& part : parts) {
        const QStringList xy = part.split(QLatin1Char(':'));
        if (xy.size() == 2)
            c.add(xy[0].toInt(), xy[1].toInt());
    }
    if (c.count == 0)
        c = FanCurve::defaultCustom();
    c.sort();
    return c;
}

} // namespace

const char* ledModeId(LedMode mode)
{
    return ledModeInfo(mode).id;
}

LedMode ledModeFromId(const QString& id)
{
    for (const auto& info : kLedModes) {
        if (id == QLatin1String(info.id))
            return info.mode;
    }
    return LedMode::Fixed;
}

QString filePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/LiquidCam.ini");
}

void load(AppSettings& out)
{
    QSettings ini(filePath(), QSettings::IniFormat);

    ini.beginGroup(QStringLiteral("lighting"));
    out.lighting.mode = ledModeFromId(ini.value(QStringLiteral("mode"),
                                                QStringLiteral("fixed")).toString());
    out.lighting.speed = static_cast<AnimSpeed>(
        qBound(0, ini.value(QStringLiteral("speed"), 2).toInt(), 4));
    out.lighting.backward   = ini.value(QStringLiteral("backward"), false).toBool();
    out.lighting.brightness = static_cast<uint8_t>(
        qBound(0, ini.value(QStringLiteral("brightness"), 100).toInt(), 100));
    out.lighting.colorCount = static_cast<uint8_t>(
        qBound(1, ini.value(QStringLiteral("colorCount"), 1).toInt(), kMaxLedColors));
    for (int i = 0; i < kMaxLedColors; ++i) {
        const QString key = QStringLiteral("color%1").arg(i);
        out.lighting.colors[i] = hexToColor(ini.value(key, QStringLiteral("8B5CF6")).toString());
    }
    out.applyLightingAtStartup = ini.value(QStringLiteral("applyAtStartup"), true).toBool();
    ini.endGroup();

    ini.beginGroup(QStringLiteral("cooling"));
    for (int i = 0; i < kFanChannels; ++i) {
        ini.beginGroup(QStringLiteral("fan%1").arg(i + 1));
        out.channels[i].mode = static_cast<FanMode>(
            qBound(0, ini.value(QStringLiteral("mode"), 0).toInt(), 3));
        out.channels[i].fixedDuty =
            qBound(0, ini.value(QStringLiteral("fixedDuty"), 40).toInt(), 100);
        out.channels[i].curve = curveFromString(
            ini.value(QStringLiteral("curve"), curveToString(FanCurve::defaultCustom())).toString());
        ini.endGroup();
    }
    out.applyFansAtStartup = ini.value(QStringLiteral("applyAtStartup"), true).toBool();
    out.curveSource        = static_cast<CurveSource>(
        qBound(0, ini.value(QStringLiteral("curveSource"), 0).toInt(), 2));
    out.fallbackTemp = qBound(0, ini.value(QStringLiteral("fallbackTemp"), 45).toInt(), 100);
    out.minDuty      = qBound(0, ini.value(QStringLiteral("minDuty"), 20).toInt(), 100);
    ini.endGroup();

    ini.beginGroup(QStringLiteral("psu"));
    out.psuFanMode  = static_cast<PsuFanMode>(
        qBound(0, ini.value(QStringLiteral("fanMode"), 0).toInt(), 4));
    out.psuFixedPct = qBound(20, ini.value(QStringLiteral("fixedPct"), 40).toInt(), 100);
    out.psuCurve    = curveFromString(
        ini.value(QStringLiteral("curve"), curveToString(FanCurve::silent())).toString());
    ini.endGroup();

    ini.beginGroup(QStringLiteral("polling"));
    out.pollIntervalMs = qBound(250, ini.value(QStringLiteral("intervalMs"), 1000).toInt(), 10000);
    out.psuPollEvery   = qBound(1, ini.value(QStringLiteral("psuEvery"), 3).toInt(), 60);
    out.idleMultiplier = qBound(1, ini.value(QStringLiteral("idleMultiplier"), 3).toInt(), 20);
    ini.endGroup();

    ini.beginGroup(QStringLiteral("windows"));
    out.startMinimized = ini.value(QStringLiteral("startMinimized"), false).toBool();
    out.minimizeToTray = ini.value(QStringLiteral("minimizeToTray"), true).toBool();
    ini.endGroup();

    // The registry is the source of truth for this one, not the INI.
    out.startWithWindows = runAtStartup();
}

void save(const AppSettings& in)
{
    QSettings ini(filePath(), QSettings::IniFormat);

    ini.beginGroup(QStringLiteral("lighting"));
    ini.setValue(QStringLiteral("mode"), QLatin1String(ledModeId(in.lighting.mode)));
    ini.setValue(QStringLiteral("speed"), static_cast<int>(in.lighting.speed));
    ini.setValue(QStringLiteral("backward"), in.lighting.backward);
    ini.setValue(QStringLiteral("brightness"), static_cast<int>(in.lighting.brightness));
    ini.setValue(QStringLiteral("colorCount"), static_cast<int>(in.lighting.colorCount));
    for (int i = 0; i < kMaxLedColors; ++i)
        ini.setValue(QStringLiteral("color%1").arg(i), colorToHex(in.lighting.colors[i]));
    ini.setValue(QStringLiteral("applyAtStartup"), in.applyLightingAtStartup);
    ini.endGroup();

    ini.beginGroup(QStringLiteral("cooling"));
    for (int i = 0; i < kFanChannels; ++i) {
        ini.beginGroup(QStringLiteral("fan%1").arg(i + 1));
        ini.setValue(QStringLiteral("mode"), static_cast<int>(in.channels[i].mode));
        ini.setValue(QStringLiteral("fixedDuty"), in.channels[i].fixedDuty);
        ini.setValue(QStringLiteral("curve"), curveToString(in.channels[i].curve));
        ini.endGroup();
    }
    ini.setValue(QStringLiteral("applyAtStartup"), in.applyFansAtStartup);
    ini.setValue(QStringLiteral("curveSource"), static_cast<int>(in.curveSource));
    ini.setValue(QStringLiteral("fallbackTemp"), in.fallbackTemp);
    ini.setValue(QStringLiteral("minDuty"), in.minDuty);
    ini.endGroup();

    ini.beginGroup(QStringLiteral("psu"));
    ini.setValue(QStringLiteral("fanMode"),  int(in.psuFanMode));
    ini.setValue(QStringLiteral("fixedPct"), in.psuFixedPct);
    ini.setValue(QStringLiteral("curve"),    curveToString(in.psuCurve));
    ini.endGroup();

    ini.beginGroup(QStringLiteral("polling"));
    ini.setValue(QStringLiteral("intervalMs"), in.pollIntervalMs);
    ini.setValue(QStringLiteral("psuEvery"), in.psuPollEvery);
    ini.setValue(QStringLiteral("idleMultiplier"), in.idleMultiplier);
    ini.endGroup();

    ini.beginGroup(QStringLiteral("windows"));
    ini.setValue(QStringLiteral("startMinimized"), in.startMinimized);
    ini.setValue(QStringLiteral("minimizeToTray"), in.minimizeToTray);
    ini.endGroup();

    ini.sync();
}

bool runAtStartup()
{
    QSettings run(QLatin1String(kRunKey), QSettings::NativeFormat);
    return !run.value(QLatin1String(kRunValue)).toString().isEmpty();
}

void setRunAtStartup(bool enabled)
{
    QSettings run(QLatin1String(kRunKey), QSettings::NativeFormat);
    if (enabled) {
        const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        run.setValue(QLatin1String(kRunValue),
                     QStringLiteral("\"%1\" --minimized").arg(exe));
    } else {
        run.remove(QLatin1String(kRunValue));
    }
    run.sync();
}

} // namespace settings
} // namespace lc
