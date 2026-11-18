#pragma once

#include "platform/StartupRegistration.h"

class AppConfig;
class AppUpdater;
class NotificationSender;
class QMenu;

class AppPreferences {
public:
    AppPreferences(AppConfig &config, NotificationSender &notifications, AppUpdater &updater, QMenu &menu);
    // Returns true when the user chose Open Setup on the first-launch dialog.
    bool promptOnFirstLaunch();
    bool notificationsEnabled() const;

private:
    AppConfig &config;
    NotificationSender &notifications;
    AppUpdater &updater;
    StartupRegistration startup;

    bool showDialog(bool firstLaunch);
};
