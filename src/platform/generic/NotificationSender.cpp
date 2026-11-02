#include "platform/NotificationSender.h"

#include <QSystemTrayIcon>

struct NotificationSender::Platform {
    QSystemTrayIcon &tray;
};

NotificationSender::NotificationSender(QSystemTrayIcon &tray)
    : platform(std::make_unique<Platform>(Platform{tray}))
{
}

NotificationSender::~NotificationSender() = default;

void NotificationSender::requestPermission() {}

void NotificationSender::send(const QString &title, const QString &message)
{
    if (QSystemTrayIcon::supportsMessages())
        platform->tray.showMessage(title, message, QSystemTrayIcon::Information);
}
