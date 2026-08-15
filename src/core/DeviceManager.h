// LiquidCam - DeviceManager.h
// Owns every HID handle and the single worker thread that touches them.
//
// Threading contract:
//   * The GUI thread only ever calls the public methods below. They push a
//     command onto a queue and return immediately - no HID call ever runs on
//     the UI thread, so the interface cannot stall on a slow device.
//   * The worker publishes a Snapshot under a mutex and emits updated().
//     The signal crosses threads as a queued connection.
//   * The worker sleeps on a condition variable between ticks and blocks in
//     the kernel during reads, so idle cost is a couple of wakeups per second.
#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "app/Settings.h"
#include "core/SeasonicEPsu.h"
#include "core/SmartDeviceV1.h"
#include "core/TempMonitor.h"
#include "core/Types.h"

namespace lc {

class DeviceManager : public QObject {
    Q_OBJECT

public:
    explicit DeviceManager(QObject* parent = nullptr);
    ~DeviceManager() override;

    void start(const AppSettings& settings);
    void stop();

    Snapshot snapshot() const;

    void applyConfig(const AppSettings& settings);           // poll rates, curve source
    void setChannel(int channel, const ChannelSettings& cs); // fan mode for one header
    void applyLighting(const LightingConfig& cfg);
    void reinitialize();                                     // re-run detection
    void setUiVisible(bool visible);                         // throttles while hidden

signals:
    void updated();
    void log(const QString& message);

private:
    enum class CmdType : uint8_t { Config, Channel, Lighting, Reinit };

    struct Command {
        CmdType         type = CmdType::Reinit;
        int             channel = 0;
        ChannelSettings channelCfg;
        LightingConfig  lighting;
        AppSettings     config;
    };

    void push(Command&& cmd);
    void threadMain();
    void drainCommands();
    void openDevices(bool force);
    void pollDevices();
    void driveFans();
    void drivePsuFan();
    void publish();

    void emitLog(const QString& message);

    SmartDeviceV1 smart_;
    SeasonicEPsu  psu_;
    TempMonitor   temps_;

    AppSettings   config_;                 // worker-thread copy
    Snapshot      working_;                // worker-thread scratch
    Snapshot      published_;              // guarded by snapshotMutex_
    mutable std::mutex snapshotMutex_;

    std::thread             thread_;
    std::mutex              queueMutex_;
    std::condition_variable queueCv_;
    std::deque<Command>     queue_;

    std::atomic<bool> running_{ false };
    std::atomic<bool> uiVisible_{ true };

    std::array<int, kFanChannels>  appliedDuty_{ { -1, -1, -1 } };
    std::array<int, kFanChannels>  refreshCountdown_{ { 0, 0, 0 } };
    uint32_t tick_           = 0;
    int      reopenCooldown_ = 0;
    int      silentTicks_    = 0;   // consecutive polls with no status report
    int      psuFailures_    = 0;   // consecutive failed power supply sweeps
    bool     psuD3Logged_    = false;
    int      psuDutyWritten_ = -1;
    bool     psuOverheatLogged_ = false;

    // Hand the fan to full speed well before anything is at risk, and hold it
    // there until the unit is properly cool again.
    static constexpr float kPsuFanFullSpeedC = 60.0f;
    static constexpr float kPsuFanReleaseC   = 52.0f;
};

} // namespace lc
