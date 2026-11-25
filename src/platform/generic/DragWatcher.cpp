#include "platform/DragWatcher.h"

// Windows has no system drag pasteboard to inspect, so there is no drop panel yet.
struct DragWatcher::Impl {};

DragWatcher::DragWatcher(QSystemTrayIcon &, Targets, Dropped) {}
DragWatcher::~DragWatcher() = default;
