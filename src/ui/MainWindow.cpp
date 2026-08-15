// LiquidCam - MainWindow.cpp
#include "MainWindow.h"
#include "FanCurveWidget.h"
#include "Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace lc {
namespace {

constexpr int kLightingDebounceMs = 180;
constexpr int kSaveDebounceMs     = 900;

QColor toQColor(Rgb c) { return QColor(c.r, c.g, c.b); }
Rgb    toRgb(const QColor& c)
{
    return Rgb{ static_cast<uint8_t>(c.red()),
                static_cast<uint8_t>(c.green()),
                static_cast<uint8_t>(c.blue()) };
}

QFrame* makeCard(QVBoxLayout*& bodyOut, const QString& title = QString())
{
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("Card"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    if (!title.isEmpty()) {
        auto* label = new QLabel(title);
        label->setObjectName(QStringLiteral("CardTitle"));
        layout->addWidget(label);
    }
    bodyOut = layout;
    return card;
}

QLabel* makeReadout(const QString& text, int pointSize, const QColor& color)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("Metric"));
    label->setFont(theme::readoutFont(pointSize, true));
    // Inline stylesheet, not a palette: the global sheet sets `color` on
    // QWidget and a stylesheet always wins over a palette in Qt.
    label->setStyleSheet(QStringLiteral("color:%1;").arg(color.name()));
    return label;
}

QLabel* makeCaption(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("Caption"));
    return label;
}

QLabel* makeSectionLabel(const QString& text)
{
    auto* label = new QLabel(text.toUpper());
    label->setObjectName(QStringLiteral("SectionLabel"));
    return label;
}

} // namespace

// ---------------------------------------------------------------------------
MainWindow::MainWindow(bool startHidden, QWidget* parent)
    : QMainWindow(parent)
{
    settings::load(settings_);

    setWindowTitle(QStringLiteral("LiquidCam"));
    setWindowIcon(theme::appIcon());
    resize(980, 660);
    // Deliberately smaller than the pages need. A minimum that lies about the
    // content is worse than a scrollbar: Qt hands the layout less than its
    // minimum and the rows start drawing on top of each other.
    setMinimumSize(720, 460);

    auto* central = new QWidget;
    auto* root    = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(buildSidebar());

    auto* right       = new QWidget;
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(buildHeader());

    pages_ = new QStackedWidget;
    pages_->addWidget(buildLightingPage());
    pages_->addWidget(buildCoolingPage());
    pages_->addWidget(buildPowerPage());
    pages_->addWidget(buildPreferencesPage());
    auto* pageScroll = new QScrollArea;
    pageScroll->setObjectName(QStringLiteral("PageScroll"));
    pageScroll->setFrameShape(QFrame::NoFrame);
    pageScroll->setWidgetResizable(true);
    pageScroll->setWidget(pages_);
    rightLayout->addWidget(pageScroll, 1);

    root->addWidget(right, 1);
    setCentralWidget(central);

    lightingTimer_ = new QTimer(this);
    lightingTimer_->setSingleShot(true);
    lightingTimer_->setInterval(kLightingDebounceMs);
    connect(lightingTimer_, &QTimer::timeout, this, &MainWindow::onLightingCommit);

    saveTimer_ = new QTimer(this);
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(kSaveDebounceMs);
    connect(saveTimer_, &QTimer::timeout, this, [this] { settings::save(settings_); });

    buildTray();
    applySettingsToUi();

    devices_ = new DeviceManager(this);
    connect(devices_, &DeviceManager::updated, this, &MainWindow::onSnapshot);
    connect(devices_, &DeviceManager::log, this, &MainWindow::onLog);
    devices_->start(settings_);

    navButtons_[0]->setChecked(true);
    pages_->setCurrentIndex(0);
    pageTitle_->setText(QStringLiteral("Lighting"));

    if (!startHidden)
        show();
    else
        onLog(QStringLiteral("Started minimised to the notification area."));
}

MainWindow::~MainWindow()
{
    if (devices_)
        devices_->stop();
    settings::save(settings_);
}

// ---------------------------------------------------------------------------
// Chrome
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildSidebar()
{
    auto* sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("Sidebar"));
    sidebar->setFixedWidth(196);

    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(0, 0, 0, 14);
    layout->setSpacing(0);

    auto* wordmark = new QLabel(QStringLiteral("LIQUIDCAM"));
    wordmark->setObjectName(QStringLiteral("Wordmark"));
    layout->addWidget(wordmark);

    auto* tagline = new QLabel(QStringLiteral("NZXT CONTROL"));
    tagline->setObjectName(QStringLiteral("Tagline"));
    layout->addWidget(tagline);

    const QString names[4] = { QStringLiteral("Lighting"), QStringLiteral("Cooling"),
                               QStringLiteral("Power"),    QStringLiteral("Preferences") };
    for (int i = 0; i < 4; ++i) {
        auto* button = new QToolButton;
        button->setObjectName(QStringLiteral("NavButton"));
        button->setText(names[i]);
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        connect(button, &QToolButton::clicked, this, [this, i, names] {
            pages_->setCurrentIndex(i);
            pageTitle_->setText(names[i]);
        });
        navButtons_[i] = button;
        layout->addWidget(button);
    }

    layout->addStretch(1);

    layout->addWidget(makeSectionLabel(QStringLiteral("  Devices")));
    smartStatus_ = makeCaption(QStringLiteral("  Smart Device: searching"));
    psuStatus_   = makeCaption(QStringLiteral("  Power supply: searching"));
    smartStatus_->setContentsMargins(14, 4, 8, 0);
    psuStatus_->setContentsMargins(14, 0, 8, 0);
    layout->addWidget(smartStatus_);
    layout->addWidget(psuStatus_);

    return sidebar;
}

QWidget* MainWindow::buildHeader()
{
    auto* header = new QFrame;
    header->setObjectName(QStringLiteral("HeaderBar"));
    header->setFixedHeight(72);

    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(24, 0, 24, 0);
    layout->setSpacing(28);

    pageTitle_ = new QLabel(QStringLiteral("Lighting"));
    pageTitle_->setObjectName(QStringLiteral("PageTitle"));
    layout->addWidget(pageTitle_);
    layout->addStretch(1);

    // The readout strip: live instrument values, monospaced so digits never
    // jitter. Grouped into one widget so it can step aside when space is tight.
    headerReadouts_ = new QWidget;
    auto* readouts = new QHBoxLayout(headerReadouts_);
    readouts->setContentsMargins(0, 0, 0, 0);
    readouts->setSpacing(28);

    auto addReadout = [&](const QString& caption, QLabel*& target, const QColor& color) {
        auto* column = new QVBoxLayout;
        column->setSpacing(1);
        auto* captionLabel = makeCaption(caption);
        captionLabel->setAlignment(Qt::AlignRight);
        target = makeReadout(QStringLiteral("--"), 13, color);
        target->setAlignment(Qt::AlignRight);
        column->addWidget(captionLabel);
        column->addWidget(target);
        readouts->addLayout(column);
    };

    addReadout(QStringLiteral("CPU"),    headerSensor_, theme::kText);
    addReadout(QStringLiteral("FANS"),   headerRpm_,    theme::kText);
    addReadout(QStringLiteral("NOISE"),  headerNoise_,  theme::kTextMuted);
    addReadout(QStringLiteral("SYSTEM"), headerPower_,  theme::kGood);

    layout->addWidget(headerReadouts_);
    return header;
}

void MainWindow::buildTray()
{
    tray_ = new QSystemTrayIcon(theme::appIcon(), this);
    tray_->setToolTip(QStringLiteral("LiquidCam"));

    auto* menu = new QMenu(this);
    menu->addAction(QStringLiteral("Open LiquidCam"), this, &MainWindow::showAndRaise);
    menu->addAction(QStringLiteral("Reapply lighting"), this, [this] {
        devices_->applyLighting(settings_.lighting);
    });
    menu->addAction(QStringLiteral("Detect devices again"), this, &MainWindow::onRedetect);
    menu->addSeparator();
    menu->addAction(QStringLiteral("Quit"), this, [] { qApp->quit(); });
    tray_->setContextMenu(menu);

    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                    showAndRaise();
            });
    tray_->show();
}

// ---------------------------------------------------------------------------
// Lighting page
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildLightingPage()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);

    // Preset card ----------------------------------------------------------
    QVBoxLayout* presetBody = nullptr;
    QFrame* presetCard = makeCard(presetBody, QStringLiteral("Preset"));

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(10);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);

    modeCombo_ = new QComboBox;
    for (const auto& info : kLedModes)
        modeCombo_->addItem(QString::fromLatin1(info.label));
    grid->addWidget(makeCaption(QStringLiteral("Effect")), 0, 0);
    grid->addWidget(modeCombo_, 0, 1);

    speedCombo_ = new QComboBox;
    speedCombo_->addItems({ QStringLiteral("Slowest"), QStringLiteral("Slower"),
                            QStringLiteral("Normal"),  QStringLiteral("Faster"),
                            QStringLiteral("Fastest") });
    grid->addWidget(makeCaption(QStringLiteral("Animation speed")), 0, 2);
    grid->addWidget(speedCombo_, 0, 3);

    colorCountSpin_ = new QSpinBox;
    colorCountSpin_->setRange(1, kMaxLedColors);
    colorCountLabel_ = makeCaption(QStringLiteral("Colours in the sequence"));
    grid->addWidget(colorCountLabel_, 1, 0);
    grid->addWidget(colorCountSpin_, 1, 1);

    reverseCheck_ = new QCheckBox(QStringLiteral("Run the animation backwards"));
    grid->addWidget(reverseCheck_, 1, 2, 1, 2);

    presetBody->addLayout(grid);
    layout->addWidget(presetCard);

    // Colour card ----------------------------------------------------------
    QVBoxLayout* colorBody = nullptr;
    QFrame* colorCard = makeCard(colorBody, QStringLiteral("Colours"));

    auto* swatchRow = new QHBoxLayout;
    swatchRow->setSpacing(8);
    for (int i = 0; i < kMaxLedColors; ++i) {
        auto* button = new QPushButton;
        button->setObjectName(QStringLiteral("Swatch"));
        button->setFixedSize(44, 30);
        button->setProperty("slot", i);
        button->setToolTip(QStringLiteral("Choose colour %1").arg(i + 1));
        connect(button, &QPushButton::clicked, this, &MainWindow::onSwatchClicked);
        swatches_[i] = button;
        swatchRow->addWidget(button);
    }
    swatchRow->addStretch(1);
    colorBody->addLayout(swatchRow);

    auto* brightnessRow = new QHBoxLayout;
    brightnessRow->setSpacing(12);
    brightnessRow->addWidget(makeCaption(QStringLiteral("Brightness")));
    brightnessSlider_ = new QSlider(Qt::Horizontal);
    brightnessSlider_->setRange(0, 100);
    brightnessRow->addWidget(brightnessSlider_, 1);
    brightnessValue_ = makeReadout(QStringLiteral("100%"), 10, theme::kText);
    brightnessValue_->setFixedWidth(46);
    brightnessValue_->setAlignment(Qt::AlignRight);
    brightnessRow->addWidget(brightnessValue_);
    colorBody->addLayout(brightnessRow);

    preview_ = new QFrame;
    preview_->setFixedHeight(46);
    colorBody->addWidget(preview_);
    colorBody->addWidget(makeCaption(
        QStringLiteral("The device has no brightness register, so dimming is applied to the "
                       "colour values before they are sent.")));

    layout->addWidget(colorCard);

    // Startup card ---------------------------------------------------------
    QVBoxLayout* startupBody = nullptr;
    QFrame* startupCard = makeCard(startupBody);
    lightingStartup_ = new QCheckBox(QStringLiteral("Restore this lighting when LiquidCam starts"));
    startupBody->addWidget(lightingStartup_);

    auto* applyRow = new QHBoxLayout;
    applyRow->addStretch(1);
    auto* applyButton = new QPushButton(QStringLiteral("Apply now"));
    applyButton->setObjectName(QStringLiteral("Primary"));
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::onLightingCommit);
    applyRow->addWidget(applyButton);
    startupBody->addLayout(applyRow);

    layout->addWidget(startupCard);
    layout->addStretch(1);

    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onLightingEdited);
    connect(speedCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onLightingEdited);
    connect(colorCountSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onLightingEdited);
    connect(reverseCheck_, &QCheckBox::toggled, this, &MainWindow::onLightingEdited);
    connect(brightnessSlider_, &QSlider::valueChanged, this, &MainWindow::onLightingEdited);
    connect(lightingStartup_, &QCheckBox::toggled, this, &MainWindow::onLightingEdited);

    return page;
}

// ---------------------------------------------------------------------------
// Cooling page
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildCoolingPage()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);

    auto* row = new QHBoxLayout;
    row->setSpacing(14);

    for (int ch = 0; ch < kFanChannels; ++ch) {
        QVBoxLayout* body = nullptr;
        QFrame* card = makeCard(body, QStringLiteral("Fan %1").arg(ch + 1));

        ChannelWidgets& ui = channelUi_[ch];

        ui.rpm = makeReadout(QStringLiteral("---"), 20, theme::kText);
        auto* rpmRow = new QHBoxLayout;
        rpmRow->setSpacing(6);
        rpmRow->addWidget(ui.rpm);
        auto* unit = new QLabel(QStringLiteral("rpm"));
        unit->setObjectName(QStringLiteral("MetricUnit"));
        rpmRow->addWidget(unit, 0, Qt::AlignBottom);
        rpmRow->addStretch(1);
        body->addLayout(rpmRow);

        ui.detail = makeCaption(QStringLiteral("Not detected"));
        body->addWidget(ui.detail);

        ui.mode = new QComboBox;
        ui.mode->addItems({ QStringLiteral("Fixed"), QStringLiteral("Silent"),
                            QStringLiteral("Performance"), QStringLiteral("Custom curve") });
        ui.mode->setProperty("channel", ch);
        body->addWidget(ui.mode);

        auto* dutyRow = new QHBoxLayout;
        dutyRow->setSpacing(10);
        ui.duty = new QSlider(Qt::Horizontal);
        ui.duty->setRange(0, 100);
        ui.duty->setProperty("channel", ch);
        ui.dutyValue = makeReadout(QStringLiteral("40%"), 10, theme::kTextMuted);
        ui.dutyValue->setFixedWidth(42);
        ui.dutyValue->setAlignment(Qt::AlignRight);
        dutyRow->addWidget(ui.duty, 1);
        dutyRow->addWidget(ui.dutyValue);
        body->addLayout(dutyRow);

        connect(ui.mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::onChannelEdited);
        connect(ui.duty, &QSlider::valueChanged, this, &MainWindow::onChannelEdited);

        row->addWidget(card, 1);
    }
    layout->addLayout(row);

    QVBoxLayout* curveBody = nullptr;
    QFrame* curveCard = makeCard(curveBody, QStringLiteral("Custom curve"));

    auto* curveHeader = new QHBoxLayout;
    curveHeader->setSpacing(10);
    curveHeader->addWidget(makeCaption(QStringLiteral("Editing")));
    curveChannelCombo_ = new QComboBox;
    curveChannelCombo_->addItems({ QStringLiteral("Fan 1"), QStringLiteral("Fan 2"),
                                   QStringLiteral("Fan 3") });
    curveChannelCombo_->setFixedWidth(110);
    curveHeader->addWidget(curveChannelCombo_);
    curveHint_ = makeCaption(QStringLiteral(
        "Drag a point to move it, double-click to add one, right-click to remove."));
    curveHeader->addWidget(curveHint_);
    curveHeader->addStretch(1);
    curveBody->addLayout(curveHeader);

    curveWidget_ = new FanCurveWidget;
    curveBody->addWidget(curveWidget_, 1);

    connect(curveChannelCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onCurveChannelChanged);
    connect(curveWidget_, &FanCurveWidget::curveEdited, this, &MainWindow::onCurveEdited);

    layout->addWidget(curveCard, 1);
    return page;
}

// ---------------------------------------------------------------------------
// Power page
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildPowerPage()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);

    QVBoxLayout* summaryBody = nullptr;
    QFrame* summaryCard = makeCard(summaryBody);

    psuModel_ = new QLabel(QStringLiteral("No power supply detected"));
    psuModel_->setObjectName(QStringLiteral("CardTitle"));
    summaryBody->addWidget(psuModel_);

    auto* metrics = new QHBoxLayout;
    metrics->setSpacing(38);
    auto addMetric = [&](const QString& caption, QLabel*& target, const QString& initial,
                         const QColor& color) {
        auto* column = new QVBoxLayout;
        column->setSpacing(2);
        column->addWidget(makeCaption(caption));
        target = makeReadout(initial, 18, color);
        column->addWidget(target);
        metrics->addLayout(column);
    };
    addMetric(QStringLiteral("Total output"), psuTotal_, QStringLiteral("--- W"), theme::kGood);
    addMetric(QStringLiteral("Temperature"),  psuTemp_,  QStringLiteral("--- \u00B0C"), theme::kText);
    addMetric(QStringLiteral("Fan"),          psuFan_,   QStringLiteral("--- rpm"), theme::kText);
    metrics->addStretch(1);
    summaryBody->addLayout(metrics);

    psuFirmware_ = makeCaption(QStringLiteral("Firmware --"));
    summaryBody->addWidget(psuFirmware_);
    layout->addWidget(summaryCard);

    QVBoxLayout* railBody = nullptr;
    QFrame* railCard = makeCard(railBody, QStringLiteral("Rails"));

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(0, 1);
    grid->addWidget(makeSectionLabel(QStringLiteral("Rail")),    0, 0);
    grid->addWidget(makeSectionLabel(QStringLiteral("Voltage")), 0, 1);
    grid->addWidget(makeSectionLabel(QStringLiteral("Current")), 0, 2);
    grid->addWidget(makeSectionLabel(QStringLiteral("Power")),   0, 3);

    for (int i = 0; i < kPsuRails; ++i) {
        grid->addWidget(new QLabel(QString::fromLatin1(kPsuRailNames[i])), i + 1, 0);
        railVolts_[i] = makeReadout(QStringLiteral("--"), 11, theme::kText);
        railAmps_[i]  = makeReadout(QStringLiteral("--"), 11, theme::kText);
        railWatts_[i] = makeReadout(QStringLiteral("--"), 11, theme::kTextMuted);
        railVolts_[i]->setAlignment(Qt::AlignRight);
        railAmps_[i]->setAlignment(Qt::AlignRight);
        railWatts_[i]->setAlignment(Qt::AlignRight);
        grid->addWidget(railVolts_[i], i + 1, 1);
        grid->addWidget(railAmps_[i],  i + 1, 2);
        grid->addWidget(railWatts_[i], i + 1, 3);
    }
    railBody->addLayout(grid);
    railBody->addWidget(makeCaption(QStringLiteral(
        "Monitoring only. The E-series fan curve lives in the power supply firmware and "
        "LiquidCam does not write to it.")));

    layout->addWidget(railCard);
    layout->addStretch(1);
    return page;
}

// ---------------------------------------------------------------------------
// Preferences page
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildPreferencesPage()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);

    QVBoxLayout* startupBody = nullptr;
    QFrame* startupCard = makeCard(startupBody, QStringLiteral("Startup"));
    startupCheck_   = new QCheckBox(QStringLiteral("Start LiquidCam when I sign in to Windows"));
    minimizedCheck_ = new QCheckBox(QStringLiteral("Start in the notification area"));
    trayCheck_      = new QCheckBox(QStringLiteral("Keep running in the notification area when I close the window"));
    fansStartup_    = new QCheckBox(QStringLiteral("Restore fan settings at startup"));
    startupBody->addWidget(startupCheck_);
    startupBody->addWidget(minimizedCheck_);
    startupBody->addWidget(trayCheck_);
    startupBody->addWidget(fansStartup_);
    layout->addWidget(startupCard);

    QVBoxLayout* curveBody = nullptr;
    QFrame* curveCard = makeCard(curveBody, QStringLiteral("Fan curve input"));
    auto* curveGrid = new QGridLayout;
    curveGrid->setHorizontalSpacing(18);
    curveGrid->setVerticalSpacing(10);
    curveGrid->setColumnStretch(1, 1);
    curveGrid->setColumnStretch(3, 1);

    sourceCombo_ = new QComboBox;
    sourceCombo_->addItems({ QStringLiteral("CPU temperature (ACPI thermal zone)"),
                             QStringLiteral("CPU load"),
                             QStringLiteral("Fixed value") });
    curveGrid->addWidget(makeCaption(QStringLiteral("Drive curves from")), 0, 0);
    curveGrid->addWidget(sourceCombo_, 0, 1, 1, 3);

    fallbackSpin_ = new QSpinBox;
    fallbackSpin_->setRange(0, 100);
    fallbackSpin_->setSuffix(QStringLiteral(" \u00B0C"));
    curveGrid->addWidget(makeCaption(QStringLiteral("Value when no sensor is available")), 1, 0);
    curveGrid->addWidget(fallbackSpin_, 1, 1);

    minDutySpin_ = new QSpinBox;
    minDutySpin_->setRange(0, 100);
    minDutySpin_->setSuffix(QStringLiteral(" %"));
    curveGrid->addWidget(makeCaption(QStringLiteral("Never go below")), 1, 2);
    curveGrid->addWidget(minDutySpin_, 1, 3);

    curveBody->addLayout(curveGrid);
    curveBody->addWidget(makeCaption(QStringLiteral(
        "Not every board publishes an ACPI thermal zone. If the reading stays blank, switch to "
        "CPU load or a fixed value.")));
    layout->addWidget(curveCard);

    QVBoxLayout* pollBody = nullptr;
    QFrame* pollCard = makeCard(pollBody, QStringLiteral("Polling"));
    auto* pollGrid = new QGridLayout;
    pollGrid->setHorizontalSpacing(18);
    pollGrid->setVerticalSpacing(10);
    pollGrid->setColumnStretch(1, 1);
    pollGrid->setColumnStretch(3, 1);
    pollGrid->setColumnStretch(5, 1);

    pollSpin_ = new QSpinBox;
    pollSpin_->setRange(250, 10000);
    pollSpin_->setSingleStep(250);
    pollSpin_->setSuffix(QStringLiteral(" ms"));
    pollGrid->addWidget(makeCaption(QStringLiteral("Refresh every")), 0, 0);
    pollGrid->addWidget(pollSpin_, 0, 1);

    psuEverySpin_ = new QSpinBox;
    psuEverySpin_->setRange(1, 60);
    psuEverySpin_->setPrefix(QStringLiteral("every "));
    psuEverySpin_->setSuffix(QStringLiteral(" cycles"));
    pollGrid->addWidget(makeCaption(QStringLiteral("Read the power supply")), 0, 2);
    pollGrid->addWidget(psuEverySpin_, 0, 3);

    idleSpin_ = new QSpinBox;
    idleSpin_->setRange(1, 20);
    idleSpin_->setPrefix(QStringLiteral("x"));
    pollGrid->addWidget(makeCaption(QStringLiteral("Slow down when hidden")), 0, 4);
    pollGrid->addWidget(idleSpin_, 0, 5);

    pollBody->addLayout(pollGrid);
    pollBody->addWidget(makeCaption(QStringLiteral(
        "Longer intervals mean fewer USB transfers. Fan curves keep running while the window is "
        "hidden, just at the slower rate.")));
    layout->addWidget(pollCard);

    QVBoxLayout* logBody = nullptr;
    QFrame* logCard = makeCard(logBody, QStringLiteral("Activity"));
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(300);
    logView_->setMinimumHeight(110);
    logBody->addWidget(logView_);

    auto* buttons = new QHBoxLayout;
    auto* redetect = new QPushButton(QStringLiteral("Detect devices again"));
    connect(redetect, &QPushButton::clicked, this, &MainWindow::onRedetect);
    auto* openIni = new QPushButton(QStringLiteral("Show settings file"));
    connect(openIni, &QPushButton::clicked, this, [] {
        const QFileInfo info(settings::filePath());
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
    });
    buttons->addWidget(redetect);
    buttons->addWidget(openIni);
    buttons->addStretch(1);
    logBody->addLayout(buttons);

    layout->addWidget(logCard, 1);

    connect(startupCheck_,   &QCheckBox::toggled, this, &MainWindow::onPreferencesEdited);
    connect(minimizedCheck_, &QCheckBox::toggled, this, &MainWindow::onPreferencesEdited);
    connect(trayCheck_,      &QCheckBox::toggled, this, &MainWindow::onPreferencesEdited);
    connect(fansStartup_,    &QCheckBox::toggled, this, &MainWindow::onPreferencesEdited);
    connect(sourceCombo_,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPreferencesEdited);
    connect(fallbackSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onPreferencesEdited);
    connect(minDutySpin_,  QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onPreferencesEdited);
    connect(pollSpin_,     QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onPreferencesEdited);
    connect(psuEverySpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onPreferencesEdited);
    connect(idleSpin_,     QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onPreferencesEdited);

    return page;
}

// ---------------------------------------------------------------------------
// Settings <-> UI
// ---------------------------------------------------------------------------
void MainWindow::applySettingsToUi()
{
    loading_ = true;

    modeCombo_->setCurrentIndex(static_cast<int>(settings_.lighting.mode));
    speedCombo_->setCurrentIndex(static_cast<int>(settings_.lighting.speed));
    reverseCheck_->setChecked(settings_.lighting.backward);
    brightnessSlider_->setValue(settings_.lighting.brightness);
    brightnessValue_->setText(QStringLiteral("%1%").arg(settings_.lighting.brightness));
    colorCountSpin_->setValue(settings_.lighting.colorCount);
    lightingStartup_->setChecked(settings_.applyLightingAtStartup);

    for (int ch = 0; ch < kFanChannels; ++ch) {
        channelUi_[ch].mode->setCurrentIndex(static_cast<int>(settings_.channels[ch].mode));
        channelUi_[ch].duty->setValue(settings_.channels[ch].fixedDuty);
        channelUi_[ch].dutyValue->setText(QStringLiteral("%1%").arg(settings_.channels[ch].fixedDuty));
        channelUi_[ch].duty->setEnabled(settings_.channels[ch].mode == FanMode::Fixed);
    }
    curveChannelCombo_->setCurrentIndex(curveChannel_);
    curveWidget_->setCurve(settings_.channels[curveChannel_].curve);

    startupCheck_->setChecked(settings_.startWithWindows);
    minimizedCheck_->setChecked(settings_.startMinimized);
    trayCheck_->setChecked(settings_.minimizeToTray);
    fansStartup_->setChecked(settings_.applyFansAtStartup);
    sourceCombo_->setCurrentIndex(static_cast<int>(settings_.curveSource));
    fallbackSpin_->setValue(settings_.fallbackTemp);
    minDutySpin_->setValue(settings_.minDuty);
    pollSpin_->setValue(settings_.pollIntervalMs);
    psuEverySpin_->setValue(settings_.psuPollEvery);
    idleSpin_->setValue(settings_.idleMultiplier);

    loading_ = false;

    refreshSwatches();
    refreshPreview();
}

void MainWindow::refreshSwatches()
{
    const LedModeInfo& info = ledModeInfo(settings_.lighting.mode);
    const int visible = (info.maxColors == 0) ? 0 : settings_.lighting.colorCount;

    for (int i = 0; i < kMaxLedColors; ++i) {
        const bool show = (i < visible);
        swatches_[i]->setVisible(show);
        if (!show)
            continue;
        const QColor color = toQColor(settings_.lighting.colors[i]);
        swatches_[i]->setStyleSheet(
            QStringLiteral("QPushButton#Swatch{background:%1;border:1px solid #2E2E3A;"
                           "border-radius:4px;}"
                           "QPushButton#Swatch:hover{border:1px solid #E6E6EF;}")
                .arg(color.name()));
    }

    const bool multi = info.maxColors > 1;
    colorCountSpin_->setVisible(multi);
    colorCountLabel_->setVisible(multi);
    colorCountSpin_->setMaximum(multi ? info.maxColors : 1);
    colorCountSpin_->setMinimum(info.minColors > 0 ? info.minColors : 1);
    reverseCheck_->setEnabled(info.hasDirection);
    speedCombo_->setEnabled(info.mval != 0x00);
}

void MainWindow::refreshPreview()
{
    const LedModeInfo& info = ledModeInfo(settings_.lighting.mode);
    if (info.maxColors == 0) {
        preview_->setStyleSheet(QStringLiteral(
            "background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #FF0000, stop:0.17 #FFFF00,"
            " stop:0.33 #00FF00, stop:0.5 #00FFFF, stop:0.67 #0000FF, stop:0.83 #FF00FF,"
            " stop:1 #FF0000); border-radius:4px;"));
        return;
    }

    QString stops;
    const int count = qMax<int>(1, settings_.lighting.colorCount);
    for (int i = 0; i < count; ++i) {
        const Rgb scaled = scaleRgb(settings_.lighting.colors[i], settings_.lighting.brightness);
        const qreal position = (count == 1) ? qreal(i) : qreal(i) / qreal(count - 1);
        stops += QStringLiteral(", stop:%1 %2").arg(position).arg(toQColor(scaled).name());
        if (count == 1)
            stops += QStringLiteral(", stop:1 %1").arg(toQColor(scaled).name());
    }
    preview_->setStyleSheet(
        QStringLiteral("background: qlineargradient(x1:0,y1:0,x2:1,y2:0%1); border-radius:4px;")
            .arg(stops));
}

void MainWindow::scheduleSave()
{
    saveTimer_->start();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------
void MainWindow::onLightingEdited()
{
    if (loading_)
        return;

    settings_.lighting.mode       = static_cast<LedMode>(modeCombo_->currentIndex());
    settings_.lighting.speed      = static_cast<AnimSpeed>(speedCombo_->currentIndex());
    settings_.lighting.backward   = reverseCheck_->isChecked();
    settings_.lighting.brightness = static_cast<uint8_t>(brightnessSlider_->value());
    settings_.lighting.colorCount = static_cast<uint8_t>(colorCountSpin_->value());
    settings_.applyLightingAtStartup = lightingStartup_->isChecked();

    brightnessValue_->setText(QStringLiteral("%1%").arg(settings_.lighting.brightness));
    refreshSwatches();
    refreshPreview();

    lightingTimer_->start();   // coalesce slider drags into one USB write
    scheduleSave();
}

void MainWindow::onLightingCommit()
{
    devices_->applyLighting(settings_.lighting);
}

void MainWindow::onSwatchClicked()
{
    auto* button = qobject_cast<QPushButton*>(sender());
    if (!button)
        return;
    const int slot = button->property("slot").toInt();

    const QColor chosen = QColorDialog::getColor(
        toQColor(settings_.lighting.colors[slot]), this, QStringLiteral("Choose a colour"));
    if (!chosen.isValid())
        return;

    settings_.lighting.colors[slot] = toRgb(chosen);
    refreshSwatches();
    refreshPreview();
    lightingTimer_->start();
    scheduleSave();
}

void MainWindow::onChannelEdited()
{
    if (loading_)
        return;

    auto* widget = sender();
    const int channel = widget ? widget->property("channel").toInt() : 0;
    if (channel < 0 || channel >= kFanChannels)
        return;

    ChannelSettings& cs = settings_.channels[channel];
    cs.mode      = static_cast<FanMode>(channelUi_[channel].mode->currentIndex());
    cs.fixedDuty = channelUi_[channel].duty->value();

    channelUi_[channel].duty->setEnabled(cs.mode == FanMode::Fixed);
    channelUi_[channel].dutyValue->setText(QStringLiteral("%1%").arg(cs.fixedDuty));

    devices_->setChannel(channel, cs);
    scheduleSave();
}

void MainWindow::onCurveEdited()
{
    settings_.channels[curveChannel_].curve = curveWidget_->curve();
    devices_->setChannel(curveChannel_, settings_.channels[curveChannel_]);
    scheduleSave();
}

void MainWindow::onCurveChannelChanged(int index)
{
    if (index < 0 || index >= kFanChannels)
        return;
    curveChannel_ = index;
    curveWidget_->setCurve(settings_.channels[index].curve);
}

void MainWindow::onPreferencesEdited()
{
    if (loading_)
        return;

    const bool startup = startupCheck_->isChecked();
    if (startup != settings_.startWithWindows) {
        settings::setRunAtStartup(startup);
        settings_.startWithWindows = settings::runAtStartup();
        onLog(startup ? QStringLiteral("Added LiquidCam to the Windows startup list.")
                      : QStringLiteral("Removed LiquidCam from the Windows startup list."));
    }

    settings_.startMinimized     = minimizedCheck_->isChecked();
    settings_.minimizeToTray     = trayCheck_->isChecked();
    settings_.applyFansAtStartup = fansStartup_->isChecked();
    settings_.curveSource        = static_cast<CurveSource>(sourceCombo_->currentIndex());
    settings_.fallbackTemp       = fallbackSpin_->value();
    settings_.minDuty            = minDutySpin_->value();
    settings_.pollIntervalMs     = pollSpin_->value();
    settings_.psuPollEvery       = psuEverySpin_->value();
    settings_.idleMultiplier     = idleSpin_->value();

    devices_->applyConfig(settings_);
    scheduleSave();
}

void MainWindow::onRedetect()
{
    onLog(QStringLiteral("Scanning for devices."));
    devices_->reinitialize();
}

void MainWindow::onLog(const QString& message)
{
    if (logView_)
        logView_->appendPlainText(message);
}

void MainWindow::onSnapshot()
{
    const Snapshot snap = devices_->snapshot();

    // Sidebar status
    smartStatus_->setText(snap.smart.connected
        ? QStringLiteral("  Smart Device: firmware %1")
              .arg(QString::fromLatin1(snap.smart.firmware.data()))
        : QStringLiteral("  Smart Device: not found"));
    psuStatus_->setText(snap.psu.connected  ? QStringLiteral("  Power supply: online")
                        : snap.psu.present ? QStringLiteral("  Power supply: no reply")
                                           : QStringLiteral("  Power supply: not found"));

    // Header readouts
    if (settings_.curveSource == CurveSource::CpuLoad)
        headerSensor_->setText(QStringLiteral("%1%").arg(qRound(snap.cpuLoadPct)));
    else if (snap.tempValid)
        headerSensor_->setText(QStringLiteral("%1\u00B0C").arg(snap.cpuTempC, 0, 'f', 1));
    else
        headerSensor_->setText(QStringLiteral("--"));

    int totalRpm = 0, active = 0;
    for (int ch = 0; ch < kFanChannels; ++ch) {
        const FanStatus& fan = snap.smart.fans[ch];
        if (fan.controlMode != 0) {
            ++active;
            totalRpm += fan.rpm;
        }
    }
    headerRpm_->setText(active ? QStringLiteral("%1 rpm").arg(totalRpm / active)
                               : QStringLiteral("--"));
    headerNoise_->setText(snap.smart.connected ? QStringLiteral("%1 dB").arg(snap.smart.noiseDb)
                                               : QStringLiteral("--"));
    headerPower_->setText(snap.psu.connected
        ? QStringLiteral("%1 W").arg(snap.psu.totalWatts, 0, 'f', 0)
        : QStringLiteral("--"));

    // Cooling page
    static const char* kModeNames[3] = { "no fan", "DC", "PWM" };
    for (int ch = 0; ch < kFanChannels; ++ch) {
        const FanStatus& fan = snap.smart.fans[ch];
        channelUi_[ch].rpm->setText(fan.controlMode ? QString::number(fan.rpm)
                                                    : QStringLiteral("---"));
        if (fan.controlMode) {
            channelUi_[ch].detail->setText(
                QStringLiteral("%1 - %2 V - %3 A - %4% commanded")
                    .arg(QString::fromLatin1(kModeNames[fan.controlMode % 3]))
                    .arg(fan.millivolts / 1000.0, 0, 'f', 2)
                    .arg(fan.milliamps / 1000.0, 0, 'f', 2)
                    .arg(fan.duty));
        } else {
            channelUi_[ch].detail->setText(QStringLiteral("Nothing connected"));
        }
    }

    const float curveInput = (settings_.curveSource == CurveSource::CpuLoad)
                                 ? snap.cpuLoadPct
                                 : (snap.tempValid ? snap.cpuTempC
                                                   : float(settings_.fallbackTemp));
    curveWidget_->setAxisTitle(settings_.curveSource == CurveSource::CpuLoad
                                   ? QStringLiteral("CPU load (%)")
                                   : QStringLiteral("CPU temperature (\u00B0C)"));
    curveWidget_->setMarker(curveInput, snap.smart.fans[curveChannel_].duty,
                            settings_.curveSource != CurveSource::None);

    // Power page
    if (snap.psu.connected) {
        psuModel_->setText(QStringLiteral("NZXT E-series power supply"));
        psuTotal_->setText(QStringLiteral("%1 W").arg(snap.psu.totalWatts, 0, 'f', 0));
        psuTemp_->setText(QStringLiteral("%1 \u00B0C").arg(snap.psu.temperature, 0, 'f', 1));
        psuFan_->setText(QStringLiteral("%1 rpm").arg(snap.psu.fanRpm));
        psuFirmware_->setText(QStringLiteral("Firmware %1")
                                  .arg(QString::fromLatin1(snap.psu.firmware.data())));
        for (int i = 0; i < kPsuRails; ++i) {
            railVolts_[i]->setText(QStringLiteral("%1 V").arg(snap.psu.rails[i].volts, 0, 'f', 2));
            railAmps_[i]->setText(QStringLiteral("%1 A").arg(snap.psu.rails[i].amps, 0, 'f', 2));
            railWatts_[i]->setText(QStringLiteral("%1 W").arg(snap.psu.rails[i].watts, 0, 'f', 1));
        }
    } else {
        // Blank the figures rather than leaving the last good sweep on screen,
        // which reads as live data when it is not.
        psuModel_->setText(snap.psu.present
            ? QStringLiteral("Power supply detected, no readings")
            : QStringLiteral("No power supply detected"));
        const QString dash = QStringLiteral("--");
        psuTotal_->setText(dash);
        psuTemp_->setText(dash);
        psuFan_->setText(dash);
        psuFirmware_->setText(snap.psu.present
            ? QStringLiteral("Check the activity log in Preferences")
            : QString());
        for (int i = 0; i < kPsuRails; ++i) {
            railVolts_[i]->setText(dash);
            railAmps_[i]->setText(dash);
            railWatts_[i]->setText(dash);
        }
    }

    if (tray_) {
        tray_->setToolTip(QStringLiteral("LiquidCam\n%1 rpm average - %2 W")
                              .arg(active ? totalRpm / active : 0)
                              .arg(snap.psu.totalWatts, 0, 'f', 0));
    }
}

// ---------------------------------------------------------------------------
// Window behaviour
// ---------------------------------------------------------------------------
void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    // The strip is a convenience; the page underneath is not. It goes first.
    if (headerReadouts_)
        headerReadouts_->setVisible(width() >= 820);
}

void MainWindow::showAndRaise()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (settings_.minimizeToTray && tray_ && tray_->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange && isMinimized() && settings_.minimizeToTray) {
        QTimer::singleShot(0, this, &QWidget::hide);
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::hideEvent(QHideEvent* event)
{
    if (devices_)
        devices_->setUiVisible(false);
    QMainWindow::hideEvent(event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    if (devices_)
        devices_->setUiVisible(true);
    QMainWindow::showEvent(event);
}

} // namespace lc

#include "moc_MainWindow.cpp"
