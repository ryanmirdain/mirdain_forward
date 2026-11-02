#pragma once

#include "platform/StartupRegistration.h"

class AppConfig;
class NotificationSender;
class QMenu;

class AppPreferences {
public:
    AppPreferences(AppConfig &config, NotificationSender &notifications, QMenu &menu);
    void promptOnFirstLaunch();
    bool notificationsEnabled() const;

private:
    AppConfig &config;
    NotificationSender &notifications;
    StartupRegistration startup;

    void showDialog(bool firstLaunch);
};
