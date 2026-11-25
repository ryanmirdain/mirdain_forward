#include "platform/SlicerWindow.h"

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>

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

void SlicerWindow::openSession(const QString &sessionId, const QString &conversationId, qint64 organization)
{
    // The page defines the function once it has loaded, so a slicer still starting gets a few seconds.
    const auto args = QString::fromUtf8(QJsonDocument(QJsonArray{sessionId, conversationId, organization}).toJson(QJsonDocument::Compact));
    const auto script = QString("(function open(args, tries) { if (window.mirdainForwardOpenSession) window.mirdainForwardOpenSession(...args);"
        " else if (tries) setTimeout(() => open(args, tries - 1), 250) })(%1, 40)").arg(args);
    if (showingSlicer() && evaluate(script)) {
        show();
        return;
    }
    auto target = url;
    QUrlQuery query(target);
    query.removeAllQueryItems("organization");
    query.addQueryItem("organization", QString::number(organization));
    query.addQueryItem("session", sessionId);
    query.addQueryItem("conversation", conversationId);
    target.setQuery(query);
    showPage(target);
}

void SlicerWindow::showSlicer()
{
    show();
    if (!showingSlicer()) load(url);
}
