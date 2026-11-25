#pragma once

#include "platform/NotificationOverlay.h"

#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <vector>

class QSystemTrayIcon;
class QTimer;

// Forward's notifications. On macOS they appear in the card under the menu bar icon, with their
// rich parts: an indicator (spinner, progress bar, check, warning), updates in place, clicks,
// and dismissal. Elsewhere they are the system's tray notifications, title and body only, and
// only for the reasons worth interrupting for there (see showsAsSystemNotification).
class Notifications : public QObject {
public:
    // Why a notification is shown, which decides whether the system notifications of Windows
    // and Linux show it.
    enum class Reason {
        PlateProgress,      // a plate is downloading or transferring
        PlateSent,          // the printer accepted the print request
        PlateSavedToUsb,
        PlateFailed,
        GwaithProgress,     // Gwaith is slicing in the background
        GwaithDone,
        GwaithNeedsUser,    // Gwaith finished, but the user must act to send the plate
        GwaithFailed,
    };
    using Indicator = NotificationOverlay::Indicator;

    struct Notification {
        // Posting the same ID again updates that notification in place.
        QString id;
        Reason reason;
        QString title;
        QString body;
        Indicator indicator = Indicator::None;
        // 0 to 1, for Indicator::Progress.
        double progress = 0;
        // Milliseconds until it dismisses itself, or -1 to stay until dismissed.
        int timeout = -1;
        // Clicking it (macOS); without one, it isn't clickable.
        std::function<void()> activated;
        // Its × or its timeout (macOS); not called by remove.
        std::function<void()> dismissed;
    };

    explicit Notifications(QSystemTrayIcon &tray);
    ~Notifications() override;
    void post(const Notification &notification);
    // Takes a notification away without calling its dismissed handler.
    void remove(const QString &id);

private:
    QSystemTrayIcon &tray;
    NotificationOverlay overlay;
    // In the order first posted, which is the card's order.
    std::vector<Notification> shown;
    std::map<QString, QTimer *> timers;

    static bool showsAsSystemNotification(Reason reason);
    void dismiss(const QString &id);
    void refresh();
};
