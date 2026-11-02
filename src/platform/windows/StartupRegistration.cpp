#include "platform/StartupRegistration.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QSettings>
#include <QUrl>

#include <stdexcept>

static constexpr auto RunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr auto AppName = "Mirdain Forward";

StartupRegistration::Status StartupRegistration::status() const
{
    QSettings run(RunKey, QSettings::NativeFormat);
    return run.contains(AppName) ? Status::Enabled : Status::Disabled;
}

void StartupRegistration::setEnabled(bool enabled)
{
    QSettings run(RunKey, QSettings::NativeFormat);
    if (enabled) {
        const auto command = '"' + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + '"';
        // Windows limits Run entries to a 260-character command line.
        if (command.size() > 260)
            throw std::runtime_error("Move Mirdain Forward to a shorter path before enabling automatic startup.");
        run.setValue(AppName, command);
    } else {
        run.remove(AppName);
    }
    run.sync();
    if (run.status() != QSettings::NoError)
        throw std::runtime_error("Cannot update Mirdain Forward's startup registration.");
}

void StartupRegistration::openSettings() const
{
    QDesktopServices::openUrl(QUrl("ms-settings:startupapps"));
}
