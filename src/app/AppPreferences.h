#pragma once

#include "platform/StartupRegistration.h"

#include <QJsonObject>

#include <functional>

class AppConfig;
class AppUpdater;
class NotificationSender;
class QMenu;

// Launch at sign-in, plate notifications, and automatic update checks. Forward's preferences
// page sets them, first during onboarding and later from Preferences… in the menu.
class AppPreferences {
public:
    AppPreferences(AppConfig &config, NotificationSender &notifications, AppUpdater &updater, QMenu &menu,
        std::function<void()> openPage);
    bool notificationsEnabled() const;
    // For the page: {saved, launchAtSignIn, launchSupported, launchNeedsApproval, notifications,
    // updates, updatesAvailable}.
    QJsonObject toJson() const;
    void save(bool launchAtSignIn, bool notifications, bool updates);
    void openStartupSettings() const { startup.openSettings(); }

private:
    AppConfig &config;
    NotificationSender &notifications;
    AppUpdater &updater;
    StartupRegistration startup;
};
