#include "platform/AppUpdater.h"
#include "platform/UpdaterBackend.h"
#include "UpdaterConfig.h"

#include <QApplication>
#include <QMessageBox>
#include <QDir>
#include <QSettings>
#include <winsparkle.h>

namespace {
// WinSparkle invokes these callbacks from its worker thread. The transfer gate
// is atomic; all Qt UI and shutdown work is queued onto the application thread.
AppUpdater *owner = nullptr;

int __cdecl canShutdown()
{
    if (owner->beginInstallation()) return 1;
    owner->cancelInstallation();
    QMetaObject::invokeMethod(qApp, [] {
        QMessageBox::information(nullptr, "Mirdain Forward",
            "A plate is still being sent. Finish the transfer, then install the update again.");
    }, Qt::QueuedConnection);
    return 0;
}

void __cdecl requestShutdown()
{
    QMetaObject::invokeMethod(qApp, &QApplication::quit, Qt::QueuedConnection);
}

void __cdecl updateError() { owner->cancelInstallation(); }

class WinSparkleUpdater final : public UpdaterBackend {
public:
    explicit WinSparkleUpdater(AppUpdater &updater)
    {
        owner = &updater;
        if (!ForwardUpdatesConfigured) return;
        // Portable ZIPs must first be installed; an updater must replace the same app.
        QSettings installation("HKEY_CURRENT_USER\\Software\\Mirdain Systems\\Mirdain Forward", QSettings::NativeFormat);
        const auto installPath = installation.value("InstallPath").toString();
        if (installPath.isEmpty() || QDir(installPath) != QDir(QCoreApplication::applicationDirPath())) return;
        win_sparkle_set_app_details(L"Mirdain Systems", L"Mirdain Forward", ForwardAppVersion);
        win_sparkle_set_appcast_url(ForwardUpdateFeed);
        configured = win_sparkle_set_eddsa_public_key(ForwardUpdatePublicKey) == 1;
        win_sparkle_set_can_shutdown_callback(canShutdown);
        win_sparkle_set_shutdown_request_callback(requestShutdown);
        win_sparkle_set_error_callback(updateError);
        // Suppress WinSparkle's separate opt-in prompt. Preferences owns this UI.
        win_sparkle_set_automatic_check_for_updates(win_sparkle_get_automatic_check_for_updates());
    }
    ~WinSparkleUpdater() override
    {
        if (started) win_sparkle_cleanup();
        owner = nullptr;
    }
    bool available() const override { return configured; }
    void start() override { win_sparkle_init(); started = true; }
    bool canCheck() const override { return started; }
    void check() override { if (started) win_sparkle_check_update_with_ui(); }
    bool automaticChecksEnabled() const override { return win_sparkle_get_automatic_check_for_updates() != 0; }
    void setAutomaticChecksEnabled(bool enabled) override { win_sparkle_set_automatic_check_for_updates(enabled); }
private:
    bool configured = false;
    bool started = false;
};
}

std::unique_ptr<UpdaterBackend> createUpdaterBackend(AppUpdater &updater)
{
    return std::make_unique<WinSparkleUpdater>(updater);
}
