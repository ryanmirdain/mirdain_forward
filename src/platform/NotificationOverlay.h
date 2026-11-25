#pragma once

#include <QList>
#include <QString>

#include <functional>
#include <memory>

class QSystemTrayIcon;

// Forward's notifications on macOS and Windows: a card by the tray icon (on macOS under the
// menu bar icon, with a notch pointing up at it), listing one row per notification. A row shows an indicator (a spinner, a progress bar,
// a check, a warning, or a ! asking for action), a title, and a line of detail. Its ×
// dismisses it; clicking anywhere else on it activates it. A failure that can't be activated
// (nothing to open) instead expands to its whole error and back when clicked. Linux builds a stub
// that is unavailable, and Notifications uses the system's notifications there instead.
class NotificationOverlay {
public:
    // Attention (a !) asks the user to act, usually by clicking the row.
    enum class Indicator { None, Spinner, Progress, Done, Failed, Attention };
    // progress is 0 to 1, for Indicator::Progress. A row that isn't clickable shows no hover or
    // hand cursor and ignores clicks; its × still dismisses it.
    struct Item { QString id; QString title; QString body; Indicator indicator; double progress; bool clickable; };

    NotificationOverlay(QSystemTrayIcon &tray, std::function<void(const QString &id)> activate,
        std::function<void(const QString &id)> dismiss);
    ~NotificationOverlay();
    static bool available();
    // Shows these rows, or hides the card when there are none.
    void show(const QList<Item> &items);

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};
