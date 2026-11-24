#pragma once

#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

#include <functional>
#include <memory>

// A window that hosts the web slicer in the platform webview and exposes Forward's
// send/usb handlers to the page. The generic build opens the slicer in a browser instead.
class SlicerWindow {
public:
    using Reply = std::function<void(int status, const QJsonObject &body)>;
    using Handler = std::function<void(const QString &method, const QJsonObject &body, const Reply &reply)>;

    SlicerWindow(const QUrl &url, Handler handler);
    ~SlicerWindow();
    void show();

    // Sign-in leaves the window for the default browser, where password managers work.
    // Returns true when url is the Cognito sign-in page, which then opens in the browser.
    bool openSignInExternally(const QUrl &url);
    // Cognito redirects that browser to Forward's /callback; this reloads the slicer with its
    // query so the page finishes the sign-in.
    void completeSignIn(const QUrlQuery &callback);

    struct Impl;

private:
    void load(const QUrl &url);

    QUrl url;
    std::unique_ptr<Impl> impl;
};

// Hosts that stay inside the slicer window; other links open in the default browser.
inline bool isSlicerHost(const QUrl &slicer, const QString &host)
{
    return host == slicer.host() || host == "slicer.mirdainsystems.com" || host == "auth.mirdainsystems.com"
        || host == "mirdainsystems.com" || host == "www.mirdainsystems.com"
        || host == "api.mirdainsystems.com" || host == "gwaith.mirdainsystems.com";
}
