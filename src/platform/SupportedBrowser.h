#pragma once

class QUrl;

// Opens url in the default browser. On macOS, when that is Safari, which cannot reach
// Forward from an https page, an installed browser that can is used instead.
void openInSupportedBrowser(const QUrl &url);
