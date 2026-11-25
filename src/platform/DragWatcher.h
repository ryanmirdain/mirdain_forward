#pragma once

#include <QList>
#include <QStringList>

#include <functional>
#include <memory>

class QSystemTrayIcon;

// While an STL or 3MF file is dragged anywhere on screen, shows a drop panel under the menu
// bar icon with one box per printer. Models dropped on a box are passed to dropped as absolute
// paths, with that box's printer. macOS only; the other platforms build a stub that never
// shows the panel.
class DragWatcher {
public:
    // printerId is the custom printer ID, empty for a printer set up without one. With no
    // printers, the panel shows a single box whose target has an empty name.
    struct Target { QString printerId; QString name; };
    using Targets = std::function<QList<Target>()>;
    using Dropped = std::function<void(const QStringList &paths, const Target &target)>;

    DragWatcher(QSystemTrayIcon &tray, Targets targets, Dropped dropped);
    ~DragWatcher();

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};
