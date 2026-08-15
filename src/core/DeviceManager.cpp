// LiquidCam - DeviceManager.cpp
#include "DeviceManager.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace lc {
namespace {

// A duty change smaller than this is not worth a USB transfer, and stops the
// fans from hunting when the temperature wobbles by a degree.
constexpr int kDutyDeadband = 2;

// Even when nothing changes, re-assert the duty occasionally so a device that
// was power-cycled behind our back picks the setting back up.
constexpr int kRefreshTicks = 60;

} // namespace

DeviceManager::DeviceManager(QObject* parent)
    : QObject(parent)
{
}

DeviceManager::~DeviceManager()
{
    stop();
}

void DeviceManager::start(const AppSettings& settings)
{
    if (running_.load(std::memory_order_relaxed))
        return;

    config_ = settings;
    running_.store(true, std::memory_order_relaxed);
    thread_ = std::thread(&DeviceManager::threadMain, this);
}

void DeviceManager::stop()
{
    if (!running_.exchange(false))
        return;
    queueCv_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

Snapshot DeviceManager::snapshot() const
{
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return published_;
}

void DeviceManager::push(Command&& cmd)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queue_.push_back(std::move(cmd));
    }
    queueCv_.notify_one();
}

void DeviceManager::applyConfig(const AppSettings& settings)
{
    Command cmd;
    cmd.type   = CmdType::Config;
    cmd.config = settings;
    push(std::move(cmd));
}

void DeviceManager::setChannel(int channel, const ChannelSettings& cs)
{
    Command cmd;
    cmd.type       = CmdType::Channel;
    cmd.channel    = channel;
    cmd.channelCfg = cs;
    push(std::move(cmd));
}

void DeviceManager::applyLighting(const LightingConfig& cfg)
{
    Command cmd;
    cmd.type     = CmdType::Lighting;
    cmd.lighting = cfg;
    push(std::move(cmd));
}

void DeviceManager::reinitialize()
{
    Command cmd;
    cmd.type = CmdType::Reinit;
    push(std::move(cmd));
}

void DeviceManager::setUiVisible(bool visible)
{
    uiVisible_.store(visible, std::memory_order_relaxed);
    if (visible)
        queueCv_.notify_one();      // wake up for an immediate refresh
}

void DeviceManager::emitLog(const QString& message)
{
    emit log(message);
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------
void DeviceManager::threadMain()
{
    temps_.start();
    openDevices(true);

    if (config_.applyLightingAtStartup && smart_.isOpen()) {
        smart_.applyLighting(config_.lighting);
        emitLog(QStringLiteral("Applied saved lighting preset."));
    }
    if (config_.applyFansAtStartup)
        std::fill(refreshCountdown_.begin(), refreshCountdown_.end(), 0);

    while (running_.load(std::memory_order_relaxed)) {
        drainCommands();
        if (!running_.load(std::memory_order_relaxed))
            break;

        pollDevices();
        driveFans();
        publish();

        const bool visible = uiVisible_.load(std::memory_order_relaxed);
        const int  waitMs  = visible ? config_.pollIntervalMs
                                     : config_.pollIntervalMs * config_.idleMultiplier;

        std::unique_lock<std::mutex> lock(queueMutex_);
        queueCv_.wait_for(lock, std::chrono::milliseconds(waitMs),
                          [this] { return !queue_.empty() || !running_.load(std::memory_order_relaxed); });
    }

    smart_.close();
    psu_.close();
    temps_.stop();
}

void DeviceManager::drainCommands()
{
    for (;;) {
        Command cmd;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (queue_.empty())
                return;
            cmd = std::move(queue_.front());
            queue_.pop_front();
        }

        switch (cmd.type) {
        case CmdType::Config: {
            const bool lightingChanged = (cmd.config.lighting != config_.lighting);
            config_ = cmd.config;
            if (lightingChanged && smart_.isOpen())
                smart_.applyLighting(config_.lighting);
            std::fill(refreshCountdown_.begin(), refreshCountdown_.end(), 0);
            break;
        }
        case CmdType::Channel:
            if (cmd.channel >= 0 && cmd.channel < kFanChannels) {
                config_.channels[cmd.channel] = cmd.channelCfg;
                refreshCountdown_[cmd.channel] = 0;
                appliedDuty_[cmd.channel]      = -1;   // force a write
            }
            break;
        case CmdType::Lighting:
            config_.lighting = cmd.lighting;
            if (smart_.isOpen() && !smart_.applyLighting(config_.lighting))
                emitLog(QStringLiteral("Lighting write failed: %1")
                            .arg(QString::fromStdString(smart_.lastError())));
            break;
        case CmdType::Reinit:
            openDevices(true);
            break;
        }
    }
}

void DeviceManager::openDevices(bool force)
{
    if (!force && reopenCooldown_ > 0) {
        --reopenCooldown_;
        return;
    }
    reopenCooldown_ = 5;   // roughly every five ticks while disconnected

    if (!smart_.isOpen()) {
        if (smart_.open()) {
            if (smart_.initialize(working_.smart)) {
                emitLog(QStringLiteral("NZXT Smart Device V1 connected (firmware %1).")
                            .arg(QString::fromLatin1(working_.smart.firmware.data())));
                std::fill(appliedDuty_.begin(), appliedDuty_.end(), -1);
            } else {
                emitLog(QStringLiteral("Smart Device found but did not answer initialisation."));
                smart_.close();
            }
        } else {
            working_.smart = SmartDeviceStatus{};
        }
    }

    if (!psu_.isOpen()) {
        if (psu_.open()) {
            emitLog(QStringLiteral("Power supply connected."));
        } else {
            working_.psu = PsuStatus{};
        }
    }
}

void DeviceManager::pollDevices()
{
    ++tick_;

    if (!smart_.isOpen() || !psu_.isOpen())
        openDevices(false);

    if (smart_.isOpen()) {
        const int reports = smart_.poll(working_.smart, 400);
        if (reports < 0) {
            emitLog(QStringLiteral("Lost the Smart Device: %1")
                        .arg(QString::fromStdString(smart_.lastError())));
            smart_.close();
            working_.smart = SmartDeviceStatus{};
            silentTicks_ = 0;
        } else if (reports == 0) {
            // The handle is fine but nothing is streaming: the usual signature
            // of a resume from sleep. Re-arm reporting rather than waiting.
            if (++silentTicks_ >= 3) {
                silentTicks_ = 0;
                emitLog(QStringLiteral("Device went quiet, re-running detection."));
                if (!smart_.initialize(working_.smart))
                    smart_.close();
                std::fill(appliedDuty_.begin(), appliedDuty_.end(), -1);
                if (config_.applyLightingAtStartup && smart_.isOpen())
                    smart_.applyLighting(config_.lighting);
            }
        } else {
            silentTicks_ = 0;
        }
    }

    working_.psu.present = psu_.isOpen();
    if (psu_.isOpen() && (tick_ % static_cast<uint32_t>(config_.psuPollEvery)) == 0) {
        if (psu_.poll(working_.psu)) {
            psuFailures_ = 0;
        } else {
            working_.psu.connected = false;
            // Say so once rather than every sweep, and carry the reason: the
            // driver puts the head of the offending reply in the message.
            if (psuFailures_++ == 0) {
                emitLog(QStringLiteral("Power supply is not answering: %1")
                            .arg(QString::fromStdString(psu_.lastError())));
            }
            // A single bad sweep is normal when the controller is busy; only
            // drop the handle if the transport itself died.
            if (!psu_.isOpen()) {
                working_.psu = PsuStatus{};
            } else if (psuFailures_ > 3) {
                // Stale numbers on screen are worse than blanks.
                const auto firmware = working_.psu.firmware;
                working_.psu = PsuStatus{};
                working_.psu.present  = true;
                working_.psu.firmware = firmware;
            }
        }
    }

    working_.cpuLoadPct = temps_.readCpuLoad();
    float celsius = 0.f;
    if (temps_.readCpuTemp(celsius)) {
        working_.cpuTempC  = celsius;
        working_.tempValid = true;
    } else {
        working_.tempValid = false;
    }
}

void DeviceManager::driveFans()
{
    if (!smart_.isOpen())
        return;

    // Pick the curve input once per tick.
    float input = static_cast<float>(config_.fallbackTemp);
    switch (config_.curveSource) {
    case CurveSource::CpuTemperature:
        input = working_.tempValid ? working_.cpuTempC : static_cast<float>(config_.fallbackTemp);
        break;
    case CurveSource::CpuLoad:
        input = working_.cpuLoadPct;
        break;
    case CurveSource::None:
        break;
    }

    for (int ch = 0; ch < kFanChannels; ++ch) {
        const ChannelSettings& cs = config_.channels[ch];

        int duty = cs.fixedDuty;
        switch (cs.mode) {
        case FanMode::Fixed:       duty = cs.fixedDuty; break;
        case FanMode::Silent:      duty = FanCurve::silent().eval(input); break;
        case FanMode::Performance: duty = FanCurve::performance().eval(input); break;
        case FanMode::Custom:      duty = cs.curve.eval(input); break;
        }

        duty = std::clamp(duty, config_.minDuty, 100);

        const bool due     = (--refreshCountdown_[ch] <= 0);
        const bool changed = (appliedDuty_[ch] < 0) ||
                             (std::abs(duty - appliedDuty_[ch]) >= kDutyDeadband);

        if (changed || due) {
            if (smart_.setFanDuty(static_cast<uint8_t>(ch), static_cast<uint8_t>(duty))) {
                appliedDuty_[ch]              = duty;
                working_.smart.fans[ch].duty  = static_cast<uint8_t>(duty);
                refreshCountdown_[ch]         = kRefreshTicks;
            } else {
                refreshCountdown_[ch] = 5;
            }
        }
    }
}

void DeviceManager::publish()
{
    ++working_.seq;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        published_ = working_;
    }
    // While the window is hidden there is nobody to repaint; skip the trip
    // through the event loop entirely.
    if (uiVisible_.load(std::memory_order_relaxed))
        emit updated();
}

} // namespace lc

#include "moc_DeviceManager.cpp"
