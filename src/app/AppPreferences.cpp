#include "app/AppPreferences.h"
#include "app/AppConfig.h"
#include "platform/AppUpdater.h"

#include <QMenu>

AppPreferences::AppPreferences(AppConfig &config, AppUpdater &updater, QMenu &menu, std::function<void()> openPage)
    : config(config), updater(updater)
{
    menu.addAction("Preferences…", &menu, std::move(openPage));
}

bool AppPreferences::notificationsEnabled() const
{
    return config.section("preferences").value("notifications_enabled").toBool();
}

bool AppPreferences::quitOnWindowClose() const
{
    return config.section("preferences").value("quit_on_window_close").toBool();
}

QJsonObject AppPreferences::toJson() const
{
    const auto preferences = config.section("preferences");
    const auto status = startup.status();
    return {{"saved", preferences.contains("launch_at_sign_in")},
        {"launchAtSignIn", status == StartupRegistration::Status::Enabled || status == StartupRegistration::Status::RequiresApproval},
        {"launchSupported", status != StartupRegistration::Status::Unsupported},
        {"launchNeedsApproval", status == StartupRegistration::Status::RequiresApproval},
        {"notifications", notificationsEnabled()},
        // The native updater stores this setting itself.
        {"updates", updater.automaticChecksEnabled()}, {"updatesAvailable", updater.available()},
        {"quitOnClose", quitOnWindowClose()}};
}

void AppPreferences::save(bool launchAtSignIn, bool notify, bool updates, bool quitOnClose)
{
    const auto status = startup.status();
    const bool registered = status == StartupRegistration::Status::Enabled || status == StartupRegistration::Status::RequiresApproval;
    if (status != StartupRegistration::Status::Unsupported && launchAtSignIn != registered) startup.setEnabled(launchAtSignIn);
    auto preferences = config.section("preferences");
    preferences.insert("launch_at_sign_in", launchAtSignIn);
    preferences.insert("notifications_enabled", notify);
    preferences.insert("quit_on_window_close", quitOnClose);
    config.setSection("preferences", preferences);
    if (updater.available()) updater.setAutomaticChecksEnabled(updates);
}
