#pragma once

#include "platform/StartupRegistration.h"

#include <QJsonObject>

#include <functional>

class AppConfig;
class AppUpdater;
class QMenu;

// Launch at sign-in, plate notifications, automatic update checks, and whether closing the
// window quits Forward. Forward's preferences page sets them, first during onboarding and later
// from Preferences… in the menu.
class AppPreferences {
public:
    AppPreferences(AppConfig &config, AppUpdater &updater, QMenu &menu,
        std::function<void()> openPage);
    bool notificationsEnabled() const;
    // Off by default: Forward keeps running in the menu bar or tray so the web slicer in a
    // browser tab can still send to printers.
    bool quitOnWindowClose() const;
    // For the page: {saved, launchAtSignIn, launchSupported, launchNeedsApproval, notifications,
    // updates, updatesAvailable, quitOnClose}.
    QJsonObject toJson() const;
    void save(bool launchAtSignIn, bool notifications, bool updates, bool quitOnClose);
    void openStartupSettings() const { startup.openSettings(); }

private:
    AppConfig &config;
    AppUpdater &updater;
    StartupRegistration startup;
};
