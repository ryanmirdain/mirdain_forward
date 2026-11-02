#pragma once

#include <QString>

#include <memory>

class QSystemTrayIcon;

class NotificationSender {
public:
    explicit NotificationSender(QSystemTrayIcon &tray);
    ~NotificationSender();
    void requestPermission();
    void send(const QString &title, const QString &message);

private:
    struct Platform;
    std::unique_ptr<Platform> platform;
};
