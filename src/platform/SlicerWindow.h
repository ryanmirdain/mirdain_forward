#pragma once

#include "app/LocalAddress.h"

#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

#include <functional>
#include <memory>

// Forward's window: the platform webview showing the web slicer, or Forward's own pages
// (onboarding, Add Printer), and exposing Forward's send/usb handlers to the slicer. The
// generic build opens pages in a browser instead.
class SlicerWindow {
public:
    using Reply = std::function<void(int status, const QJsonObject &body)>;
    using Handler = std::function<void(const QString &method, const QJsonObject &body, const Reply &reply)>;

    SlicerWindow(const QUrl &url, Handler handler);
    ~SlicerWindow();
    // Brings the window forward with whatever page it shows, creating it with the slicer.
    void show();
    // Shows the window with the slicer, loading it when another page is showing.
    void showSlicer();
    // Shows the window with url, one of Forward's own pages.
    void showPage(const QUrl &url);
    // Changes the slicer page, such as for another organization, and reloads the slicer if it
    // is showing.
    void setSlicerUrl(const QUrl &url);
    // Opens a slicing session and its Gwaith chat in a new slicer tab: through the page's
    // window.mirdainForwardOpenSession while the slicer shows, else by loading the slicer with
    // them in its URL (?session=, &conversation=).
    void openSession(const QString &sessionId, const QString &conversationId, qint64 organization);
    // Queues models opened from Finder or Explorer, shows the window, and dispatches
    // mirdainforwardopen in the page. The page calls the requestOpenedFiles bridge method once
    // it can import them and receives disk-backed Files: on macOS through its model file input,
    // whose picker Forward answers with the queued files; on Windows as posted file handles.
    void openFiles(const QStringList &paths);
    // Like openFiles, but the page attaches the models to a new Gwaith conversation that slices
    // them and prints them on printer ({id, name}; both empty when no printer was chosen), and
    // imports them normally when Gwaith is unreachable. On macOS the page receives them through
    // its window.mirdainForwardOpen('gwaith', printer). Windows and the generic build open them
    // normally for now.
    void sendToGwaith(const QStringList &paths, const QJsonObject &printer);

    // Sign-in leaves the window for the default browser, where password managers work.
    // Returns true when url is the Cognito sign-in page, which then opens in the browser.
    bool openSignInExternally(const QUrl &url);
    // Cognito redirects that browser to Forward's /callback; this reloads the slicer with its
    // query so the page finishes the sign-in.
    void completeSignIn(const QUrlQuery &callback);
    // Tells the page that Forward signed in or out (mirdainforwardaccount), so the slicer
    // reloads its session from Forward.
    void accountChanged();

    struct Impl;

private:
    void load(const QUrl &url);
    bool showingSlicer() const;
    // Runs script in the page; false where the platform has no page to run it in.
    bool evaluate(const QString &script);

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

// Pages that stay inside the window and may use its bridge: the slicer's hosts and Forward's own.
inline bool isWindowPage(const QUrl &slicer, const QUrl &url)
{
    return isSlicerHost(slicer, url.host()) || isForwardPage(url);
}
