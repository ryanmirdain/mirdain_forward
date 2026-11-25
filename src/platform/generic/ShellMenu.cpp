#include "platform/ShellMenu.h"

// Explorer's context menu exists only on Windows; macOS sends models to Gwaith from the drop panel.
void ShellMenu::update(const QList<Printer> &) {}
