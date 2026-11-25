#include "platform/NotificationOverlay.h"

// Windows and Linux use the system's notifications (see Notifications).
struct NotificationOverlay::Impl {};

NotificationOverlay::NotificationOverlay(QSystemTrayIcon &, std::function<void(const QString &)>, std::function<void(const QString &)>) {}
NotificationOverlay::~NotificationOverlay() = default;
bool NotificationOverlay::available() { return false; }
void NotificationOverlay::show(const QList<Item> &) {}
