#pragma once

#include <QList>
#include <QString>

// Explorer's right-click Send to Gwaith submenu for .stl and .3mf files, with one entry per
// Forward printer. Choosing one launches Forward with ShellMenu::Argument, the printer's ID, and
// the file, and a running Forward takes it over (see main.cpp). Windows only: Forward writes the
// menu to the user's registry classes, and the installer removes it on uninstall. The other
// platforms build a stub.
class ShellMenu {
public:
    static constexpr auto Argument = "--gwaith-printer";
    struct Printer { QString id; QString name; };
    // Lists these printers, or removes the menu when there are none.
    static void update(const QList<Printer> &printers);
};
