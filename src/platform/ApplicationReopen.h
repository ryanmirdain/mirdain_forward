#pragma once

#include <functional>

// Calls callback when the running app is launched again (macOS sends a reopen event
// rather than starting a second process). Other platforms rely on the second instance.
void onApplicationReopen(std::function<void()> callback);
