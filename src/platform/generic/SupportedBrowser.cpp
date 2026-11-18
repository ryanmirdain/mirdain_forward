#include "platform/SupportedBrowser.h"

#include <QDesktopServices>
#include <QUrl>

void openInSupportedBrowser(const QUrl &url) { QDesktopServices::openUrl(url); }
