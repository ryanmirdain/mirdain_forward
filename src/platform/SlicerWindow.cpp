#include "platform/SlicerWindow.h"

#include <QDesktopServices>

bool SlicerWindow::openSignInExternally(const QUrl &target)
{
    // Cognito's hosted sign-in, on the custom domain or a dev pool's amazoncognito.com domain.
    if (target.path() != "/oauth2/authorize") return false;
    QDesktopServices::openUrl(target);
    return true;
}

void SlicerWindow::completeSignIn(const QUrlQuery &callback)
{
    // The page checks state against the sign-in it started before exchanging the code.
    auto target = url;
    target.setQuery(callback);
    load(target);
    show();
}

void SlicerWindow::showSlicer()
{
    show();
    if (!showingSlicer()) load(url);
}
