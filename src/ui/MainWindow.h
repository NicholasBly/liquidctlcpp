// LiquidCam - MainWindow.h
#pragma once

#include <QMainWindow>
#include <array>

#include "app/Settings.h"
#include "core/DeviceManager.h"
#include "core/Types.h"

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QSystemTrayIcon;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace lc {

class FanCurveWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(bool startHidden, QWidget* parent = nullptr);
    ~MainWindow() override;

public slots:
    void showAndRaise();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onSnapshot();
    void onLog(const QString& message);
    void onLightingEdited();
    void onLightingCommit();
    void onSwatchClicked();
    void onChannelEdited();
    void onCurveEdited();
    void onCurveChannelChanged(int index);
    void onPreferencesEdited();
    void onRedetect();

private:
    QWidget* buildSidebar();
    QWidget* buildHeader();
    QWidget* buildLightingPage();
    QWidget* buildCoolingPage();
    QWidget* buildPowerPage();
    QWidget* buildPreferencesPage();
    void     buildTray();

    void applySettingsToUi();
    void refreshSwatches();
    void refreshPreview();
    void scheduleSave();

    // --- state -------------------------------------------------------------
    AppSettings    settings_;
    DeviceManager* devices_ = nullptr;
    bool           loading_ = false;
    int            curveChannel_ = 0;

    // --- chrome ------------------------------------------------------------
    QStackedWidget* pages_          = nullptr;
    QWidget*        headerReadouts_ = nullptr;
    QLabel*         headerSensorCaption_ = nullptr;
    QLabel*         pageTitle_    = nullptr;
    QSystemTrayIcon* tray_        = nullptr;
    QTimer*         lightingTimer_ = nullptr;
    QTimer*         saveTimer_    = nullptr;
    std::array<QToolButton*, 4> navButtons_{};

    QLabel* smartStatus_ = nullptr;
    QLabel* psuStatus_   = nullptr;

    QLabel* headerSensor_ = nullptr;
    QLabel* headerRpm_    = nullptr;
    QLabel* headerNoise_  = nullptr;
    QLabel* headerPower_  = nullptr;

    // --- lighting ----------------------------------------------------------
    QComboBox* modeCombo_       = nullptr;
    QComboBox* speedCombo_      = nullptr;
    QCheckBox* reverseCheck_    = nullptr;
    QSlider*   brightnessSlider_ = nullptr;
    QLabel*    brightnessValue_ = nullptr;
    QSpinBox*  colorCountSpin_  = nullptr;
    QLabel*    colorCountLabel_ = nullptr;
    QFrame*    preview_         = nullptr;
    QCheckBox* lightingStartup_ = nullptr;
    std::array<QPushButton*, kMaxLedColors> swatches_{};

    // --- cooling -----------------------------------------------------------
    struct ChannelWidgets {
        QComboBox* mode   = nullptr;
        QSlider*   duty   = nullptr;
        QLabel*    dutyValue = nullptr;
        QLabel*    rpm    = nullptr;
        QLabel*    detail = nullptr;
    };
    std::array<ChannelWidgets, kFanChannels> channelUi_{};
    QComboBox*      curveChannelCombo_ = nullptr;
    FanCurveWidget* curveWidget_       = nullptr;
    QLabel*         curveHint_         = nullptr;

    // --- power -------------------------------------------------------------
    QLabel* psuModel_    = nullptr;
    QLabel* psuTemp_     = nullptr;
    QLabel* psuFan_      = nullptr;
    QLabel* psuTotal_    = nullptr;
    QLabel* psuFirmware_ = nullptr;
    QLabel* psuHours_    = nullptr;
    QComboBox* psuFanMode_  = nullptr;
    QSpinBox*  psuFixedPct_ = nullptr;
    QLabel*    psuFanNote_  = nullptr;
    std::array<QLabel*, kPsuRails> railVolts_{};
    std::array<QLabel*, kPsuRails> railAmps_{};
    std::array<QLabel*, kPsuRails> railWatts_{};

    // --- preferences -------------------------------------------------------
    QCheckBox*      startupCheck_    = nullptr;
    QCheckBox*      minimizedCheck_  = nullptr;
    QCheckBox*      trayCheck_       = nullptr;
    QCheckBox*      fansStartup_     = nullptr;
    QComboBox*      sourceCombo_     = nullptr;
    QSpinBox*       fallbackSpin_    = nullptr;
    QSpinBox*       minDutySpin_     = nullptr;
    QSpinBox*       pollSpin_        = nullptr;
    QSpinBox*       psuEverySpin_    = nullptr;
    QSpinBox*       idleSpin_        = nullptr;
    QPlainTextEdit* logView_         = nullptr;
};

} // namespace lc
