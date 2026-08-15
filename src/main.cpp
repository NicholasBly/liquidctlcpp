// LiquidCam - main.cpp
// A lightweight replacement for NZXT CAM covering the Smart Device V1 and the
// E-series power supplies.
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QStringList>
#include <QSystemTrayIcon>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include "app/Settings.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

namespace {

// Single instance handling without QtNetwork. A named mutex answers "is one
// already running", and a registered window message asks that copy to show
// itself. QLocalServer would do the same job, but it drags Qt5Network.dll into
// the deployment for what amounts to two Win32 calls.
//
// The Local\ prefix scopes the mutex to the logon session, so a second user on
// the same machine still gets their own instance.
const wchar_t kInstanceMutex[] = L"Local\\LiquidCam.SingleInstance";
const wchar_t kShowMessageName[] = L"LiquidCam.ShowWindow";

UINT g_showMessage = 0;

// Catches the broadcast from a second launch and brings the window forward.
class ShowRequestFilter : public QAbstractNativeEventFilter
{
public:
    explicit ShowRequestFilter(lc::MainWindow* window) : window_(window) {}

    bool nativeEventFilter(const QByteArray& type, void* message, long*) override
    {
        if (g_showMessage == 0 || type != "windows_generic_MSG")
            return false;
        const MSG* msg = static_cast<const MSG*>(message);
        if (msg->message != g_showMessage)
            return false;
        window_->showAndRaise();
        return true;
    }

private:
    lc::MainWindow* window_;
};

} // namespace

int main(int argc, char* argv[])
{
    g_showMessage = ::RegisterWindowMessageW(kShowMessageName);

    // Done before Qt is initialised: a duplicate launch should cost as close to
    // nothing as possible.
    HANDLE instanceLock = ::CreateMutexW(nullptr, TRUE, kInstanceMutex);
    if (instanceLock != nullptr && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_showMessage != 0)
            ::PostMessageW(HWND_BROADCAST, g_showMessage, 0, 0);
        ::CloseHandle(instanceLock);
        return 0;
    }

    // Set before the QApplication exists, otherwise it has no effect.
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LiquidCam"));
    QCoreApplication::setApplicationName(QStringLiteral("LiquidCam"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    app.setQuitOnLastWindowClosed(false);   // the tray icon keeps us alive
    app.setStyleSheet(lc::theme::styleSheet());
    app.setWindowIcon(lc::theme::appIcon());

    bool startHidden = false;
    const QStringList arguments = QCoreApplication::arguments();
    for (const QString& argument : arguments) {
        if (argument == QLatin1String("--minimized") || argument == QLatin1String("-m"))
            startHidden = true;
    }

    lc::AppSettings probe;
    lc::settings::load(probe);
    if (probe.startMinimized)
        startHidden = true;

    if (!QSystemTrayIcon::isSystemTrayAvailable())
        startHidden = false;        // no tray to hide in, so show the window

    lc::MainWindow window(startHidden);

    // Realise the native handle even when starting hidden. HWND_BROADCAST only
    // reaches windows that exist, visible or not, and without this a copy
    // launched at sign-in would ignore the user double-clicking the shortcut.
    window.createWinId();

    ShowRequestFilter showFilter(&window);
    app.installNativeEventFilter(&showFilter);

    const int rc = app.exec();

    app.removeNativeEventFilter(&showFilter);
    ::ReleaseMutex(instanceLock);
    ::CloseHandle(instanceLock);
    return rc;
}
