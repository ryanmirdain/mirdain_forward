#include "app/Notifications.h"

#include <QSystemTrayIcon>
#include <QTimer>

#include <algorithm>

Notifications::Notifications(QSystemTrayIcon &tray)
    : tray(tray), overlay(tray,
          [this](const QString &id) {
              const auto found = std::find_if(shown.begin(), shown.end(), [&](const Notification &item) { return item.id == id; });
              if (found != shown.end() && found->activated) found->activated();
          },
          [this](const QString &id) { dismiss(id); })
{
}

Notifications::~Notifications() = default;

// Progress, and Gwaith finishing successfully, would only interrupt on Windows and Linux:
// their notifications can't update in place or be clicked through to a run.
bool Notifications::showsAsSystemNotification(Reason reason)
{
    switch (reason) {
    case Reason::PlateSent:
    case Reason::PlateSavedToUsb:
    case Reason::PlateFailed:
    case Reason::GwaithNeedsUser:
    case Reason::GwaithFailed:
        return true;
    case Reason::PlateProgress:
    case Reason::GwaithProgress:
    case Reason::GwaithDone:
        return false;
    }
    return false;
}

void Notifications::post(const Notification &notification)
{
    if (!NotificationOverlay::available()) {
        if (showsAsSystemNotification(notification.reason) && QSystemTrayIcon::supportsMessages())
            tray.showMessage(notification.title, notification.body, QSystemTrayIcon::Information);
        return;
    }
    const auto found = std::find_if(shown.begin(), shown.end(), [&](const Notification &item) { return item.id == notification.id; });
    if (found == shown.end()) shown.push_back(notification);
    else *found = notification;
    // Each post sets the timeout afresh.
    if (const auto timer = timers.find(notification.id); timer != timers.end()) {
        timer->second->deleteLater();
        timers.erase(timer);
    }
    if (notification.timeout >= 0) {
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        const auto id = notification.id;
        connect(timer, &QTimer::timeout, this, [this, id] { dismiss(id); });
        timer->start(notification.timeout);
        timers[id] = timer;
    }
    refresh();
}

void Notifications::remove(const QString &id)
{
    if (const auto timer = timers.find(id); timer != timers.end()) {
        timer->second->deleteLater();
        timers.erase(timer);
    }
    shown.erase(std::remove_if(shown.begin(), shown.end(), [&](const Notification &item) { return item.id == id; }), shown.end());
    refresh();
}

void Notifications::dismiss(const QString &id)
{
    const auto found = std::find_if(shown.begin(), shown.end(), [&](const Notification &item) { return item.id == id; });
    if (found == shown.end()) return;
    // The handler may post again, so it runs after the notification is gone.
    const auto dismissed = found->dismissed;
    remove(id);
    if (dismissed) dismissed();
}

void Notifications::refresh()
{
    QList<NotificationOverlay::Item> items;
    for (const auto &item : shown)
        items.append({item.id, item.title, item.body, item.indicator, item.progress, bool(item.activated)});
    overlay.show(items);
}
