#pragma once

#include "platform/StartupRegistration.h"

class AppConfig;
class AppUpdater;
class NotificationSender;
class QMenu;

class AppPreferences {
public:
    AppPreferences(AppConfig &config, NotificationSender &notifications, AppUpdater &updater, QMenu &menu);
    void promptOnFirstLaunch();
    bool notificationsEnabled() const;

private:
    AppConfig &config;
    NotificationSender &notifications;
    AppUpdater &updater;
    StartupRegistration startup;

    void showDialog(bool firstLaunch);
};
