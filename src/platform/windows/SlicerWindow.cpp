#include "platform/SlicerWindow.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>

#include <WebView2.h>
#include <wrl.h>

#include <algorithm>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

QString takeString(LPWSTR value)
{
    const auto result = QString::fromWCharArray(value);
    CoTaskMemFree(value);
    return result;
}

std::wstring wide(const QString &value) { return value.toStdWString(); }

// While the window is hidden, Forward suspends the page, which frees its renderer's memory (the
// slicer holds hundreds of MB with a complex plate) and stops its scripts. It waits this long
// after hiding, so reopening the window soon after stays instant and short work finishes.
constexpr int SuspendDelay = 60 * 1000;
// A suspended slicer wakes this often, and for this long, so its keepalive reaches the server:
// a slicing session expires after 30 idle minutes. Its sockets reconnect then too.
constexpr int WakeEvery = 10 * 60 * 1000, WakeLength = 20 * 1000;

// A plain Qt window whose native HWND hosts the WebView2 controller. It has no caption:
// the page's header is the title bar, with its own caption buttons.
class HostWidget : public QWidget {
public:
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    // Bridge requests the page is waiting on Forward to answer; it isn't suspended meanwhile.
    int pendingRequests = 0;

    HostWidget()
    {
        suspendTimer.setSingleShot(true);
        wakeTimer.setSingleShot(true);
        connect(&suspendTimer, &QTimer::timeout, this, [this] { suspend(); });
        connect(&wakeTimer, &QTimer::timeout, this, [this] { resume(WakeLength); });
    }

    bool hidden() const { return !isVisible() || isMinimized(); }

    // Suspends the page after, if the window is still hidden then.
    void suspendLater(int after)
    {
        if (hidden()) suspendTimer.start(after);
    }

    // Resumes a suspended page, such as before Forward runs script in it. While the window stays
    // hidden, the page is suspended again after.
    void resume(int after = SuspendDelay)
    {
        wakeTimer.stop();
        ComPtr<ICoreWebView2_3> suspendable;
        BOOL suspended = FALSE;
        if (webview && SUCCEEDED(webview.As(&suspendable)) && SUCCEEDED(suspendable->get_IsSuspended(&suspended)) && suspended)
            suspendable->Resume();
        suspendLater(after);
    }

    // The page swaps its maximize and restore buttons on this attribute.
    void syncMaximized()
    {
        if (webview) webview->ExecuteScript(isMaximized() ? L"document.documentElement.dataset.forwardMaximized = ''"
                                                          : L"delete document.documentElement.dataset.forwardMaximized", nullptr);
    }

    void fit()
    {
        if (!controller) return;
        RECT bounds;
        GetClientRect(reinterpret_cast<HWND>(winId()), &bounds);
        controller->put_Bounds(bounds);
    }

private:
    QTimer suspendTimer;
    QTimer wakeTimer;

    // Freezing the page mid-slice, mid-upload or mid-send would break that work, so the page
    // is suspended only when Forward owes it no reply and the slicer reports no such work.
    void suspend()
    {
        if (!webview || !hidden() || pendingRequests) return;
        webview->ExecuteScript(L"!!window.mirdainForwardBusy?.()", Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [this](HRESULT, LPCWSTR busy) -> HRESULT {
                if (!hidden() || pendingRequests) return S_OK;
                ComPtr<ICoreWebView2_3> suspendable;
                // A failed check (null) counts as idle: Forward's own pages report nothing.
                if ((busy && QString::fromWCharArray(busy) == "true") || FAILED(webview.As(&suspendable))) {
                    suspendLater(SuspendDelay);
                    return S_OK;
                }
                return suspendable->TrySuspend(Callback<ICoreWebView2TrySuspendCompletedHandler>(
                    [this](HRESULT, BOOL suspended) -> HRESULT {
                        // WebView2 declines while, for one, media plays: try again later.
                        if (suspended) wakeTimer.start(WakeEvery);
                        else suspendLater(SuspendDelay);
                        return S_OK;
                    }).Get());
            }).Get());
    }

protected:
    // Closing a top-level Qt window destroys its native window, and the WebView2 controller
    // with it: shown again, the window would be blank. Closing only hides it, as Forward keeps
    // running in the tray. This covers the caption's close button, Alt+F4 and the taskbar.
    void closeEvent(QCloseEvent *event) override
    {
        event->ignore();
        hide();
    }

    bool event(QEvent *event) override
    {
        const auto handled = QWidget::event(event);
        if (!controller) return handled;
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) fit();
        // WebView2 does not follow its host's visibility: tell it, so a hidden page stops
        // rendering, and suspend the page a while later.
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide || event->type() == QEvent::WindowStateChange) {
            if (hidden()) {
                controller->put_IsVisible(FALSE);
                suspendLater(SuspendDelay);
            } else {
                suspendTimer.stop();
                resume();
                controller->put_IsVisible(TRUE);
            }
        }
        // Popups such as <select> lists are positioned from the parent window.
        if (event->type() == QEvent::Move) controller->NotifyParentWindowPositionChanged();
        if (event->type() == QEvent::WindowActivate) controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        if (event->type() == QEvent::WindowStateChange) syncMaximized();
        return handled;
    }

    bool nativeEvent(const QByteArray &, void *message, qintptr *result) override
    {
        const auto *msg = static_cast<MSG *>(message);
        if (msg->message != WM_NCCALCSIZE || !msg->wParam) return false;
        // Drop the caption but keep the side and bottom resize borders.
        auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
        const auto top = params->rgrc[0].top;
        DefWindowProcW(msg->hwnd, msg->message, msg->wParam, msg->lParam);
        params->rgrc[0].top = top;
        // A maximized window extends past the screen edge by its frame thickness.
        if (IsZoomed(msg->hwnd)) {
            const auto dpi = GetDpiForWindow(msg->hwnd);
            params->rgrc[0].top += GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        }
        *result = 0;
        return true;
    }
};

}

struct SlicerWindow::Impl {
    Impl(SlicerWindow *owner, const QUrl &url, Handler handler) : owner(owner), url(url), handler(std::move(handler)) {}
    SlicerWindow *owner;
    QUrl url;
    Handler handler;
    HostWidget window;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2> webview;
    bool started = false;
    // The page the window loads once WebView2 is ready, and the latest main-frame navigation.
    QUrl current;
    QStringList openedFiles;

    void start();
    void configure();
    void deliverOpenedFiles();
};

void SlicerWindow::Impl::start()
{
    started = true;
    window.setWindowTitle("Mirdain Slicer");
    window.setMinimumSize(800, 560);
    window.resize(1440, 900);
    // Recalculate the frame so nativeEvent's WM_NCCALCSIZE removes the caption.
    SetWindowPos(reinterpret_cast<HWND>(window.winId()), nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    // The default user data folder sits next to the executable, which may not be writable.
    const auto folder = wide(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("WebView2"));
    CreateCoreWebView2EnvironmentWithOptions(nullptr, folder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment *environment) -> HRESULT {
                if (FAILED(result)) {
                    // The WebView2 Runtime is missing; the browser still reaches Forward over HTTP.
                    window.hide();
                    started = false;
                    QDesktopServices::openUrl(current);
                    return S_OK;
                }
                this->environment = environment;
                return environment->CreateCoreWebView2Controller(reinterpret_cast<HWND>(window.winId()),
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT result, ICoreWebView2Controller *controller) -> HRESULT {
                            if (FAILED(result)) return result;
                            window.controller = controller;
                            controller->get_CoreWebView2(&webview);
                            window.webview = webview;
                            window.fit();
                            configure();
                            return webview->Navigate(wide(current.toString(QUrl::FullyEncoded)).c_str());
                        }).Get());
            }).Get());
}

void SlicerWindow::Impl::configure()
{
    EventRegistrationToken token;

    // Marks the page as Forward's slicer window; the slicer checks it before using the bridge,
    // since window.chrome.webview exists in any WebView2 host. titleBar tells the page to draw
    // caption buttons on the right of its header.
    webview->AddScriptToExecuteOnDocumentCreated(L"window.mirdainForward = { titleBar: 'right' }", nullptr);

    // The header's CSS app-region: drag areas then drag, snap and double-click like a title bar.
    ComPtr<ICoreWebView2Settings> settings;
    webview->get_Settings(&settings);
    ComPtr<ICoreWebView2Settings9> nonClientSettings;
    if (SUCCEEDED(settings.As(&nonClientSettings))) nonClientSettings->put_IsNonClientRegionSupportEnabled(TRUE);

    webview->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
        [this](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *) -> HRESULT {
            window.syncMaximized();
            return S_OK;
        }).Get(), &token);

    // Main-frame navigations away from the slicer and auth hosts open in the default browser.
    webview->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
        [this](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
            LPWSTR uri;
            args->get_Uri(&uri);
            const QUrl target(takeString(uri));
            if (owner->openSignInExternally(target)) {
                args->put_Cancel(TRUE);
                return S_OK;
            }
            if ((target.scheme() == "http" || target.scheme() == "https") && !isWindowPage(url, target)) {
                args->put_Cancel(TRUE);
                QDesktopServices::openUrl(target);
                return S_OK;
            }
            current = target;
            return S_OK;
        }).Get(), &token);

    // window.open and target=_blank: slicer pages replace this one, everything else opens in the browser.
    webview->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
        [this](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
            LPWSTR uri;
            args->get_Uri(&uri);
            const QUrl target(takeString(uri));
            args->put_Handled(TRUE);
            if (isWindowPage(url, target)) webview->Navigate(wide(target.toString()).c_str());
            else QDesktopServices::openUrl(target);
            return S_OK;
        }).Get(), &token);

    // The page posts {id, method, body}; Forward answers {id, status, body}.
    webview->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
        [this](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
            LPWSTR source;
            args->get_Source(&source);
            LPWSTR json;
            args->get_WebMessageAsJson(&json);
            const auto message = QJsonDocument::fromJson(takeString(json).toUtf8()).object();
            const auto id = message.value("id");
            const auto reply = [this, id](int status, const QJsonObject &body) {
                const QJsonObject result{{"id", id}, {"status", status}, {"body", body}};
                webview->PostWebMessageAsJson(wide(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))).c_str());
            };
            if (!isWindowPage(url, QUrl(takeString(source)))) {
                reply(403, {{"error", "Forward is not available to this page."}});
                return S_OK;
            }
            const auto method = message.value("method").toString();
            if (method == "requestOpenedFiles") {
                deliverOpenedFiles();
                reply(200, {});
                return S_OK;
            }
            // The header's caption buttons.
            if (method == "minimize" || method == "toggleMaximize" || method == "close") {
                if (method == "minimize") window.showMinimized();
                else if (method == "close") window.hide();
                else if (window.isMaximized()) window.showNormal();
                else window.showMaximized();
                reply(200, {});
                return S_OK;
            }
            window.pendingRequests++;
            handler(method, message.value("body").toObject(), [this, reply](int status, const QJsonObject &body) {
                reply(status, body);
                window.pendingRequests = std::max(0, window.pendingRequests - 1);
                if (!window.pendingRequests) window.suspendLater(SuspendDelay);
            });
            return S_OK;
        }).Get(), &token);

    // File pickers and downloads (to the user's Downloads folder) use WebView2's built-in UI.
}

// Posts the queued models as read-only FileSystemFileHandles, which the page turns into Files.
void SlicerWindow::Impl::deliverOpenedFiles()
{
    if (openedFiles.isEmpty()) return;
    ComPtr<ICoreWebView2Environment14> files;
    ComPtr<ICoreWebView2_23> poster;
    environment.As(&files);
    webview.As(&poster);
    std::vector<ComPtr<ICoreWebView2FileSystemHandle>> handles(openedFiles.size());
    std::vector<IUnknown *> items;
    for (qsizetype index = 0; index < openedFiles.size(); ++index) {
        files->CreateWebFileSystemFileHandle(wide(QDir::toNativeSeparators(openedFiles[index])).c_str(),
            COREWEBVIEW2_FILE_SYSTEM_HANDLE_PERMISSION_READ_ONLY, &handles[index]);
        items.push_back(handles[index].Get());
    }
    ComPtr<ICoreWebView2ObjectCollection> collection;
    files->CreateObjectCollection(UINT32(items.size()), items.data(), &collection);
    poster->PostWebMessageAsJsonWithAdditionalObjects(L"{\"event\":\"openedFiles\"}", collection.Get());
    openedFiles.clear();
}

SlicerWindow::SlicerWindow(const QUrl &url, Handler handler) : url(url), impl(new Impl(this, url, std::move(handler))) {}

SlicerWindow::~SlicerWindow() = default;

void SlicerWindow::load(const QUrl &target)
{
    impl->current = target;
    // Before WebView2 is ready, start() navigates to current.
    if (impl->webview) impl->webview->Navigate(wide(target.toString(QUrl::FullyEncoded)).c_str());
}

bool SlicerWindow::showingSlicer() const { return isSlicerHost(url, impl->current.host()); }

void SlicerWindow::setSlicerUrl(const QUrl &slicer)
{
    if (slicer == url) return;
    url = slicer;
    impl->url = slicer;
    if (showingSlicer()) load(url);
}

bool SlicerWindow::evaluate(const QString &script)
{
    if (!impl->webview) return false;
    impl->window.resume();
    impl->webview->ExecuteScript(wide(script).c_str(), nullptr);
    return true;
}

void SlicerWindow::accountChanged()
{
    if (!impl->webview) return;
    // Signing in finishes in the browser while the window may be hidden and the page suspended.
    impl->window.resume();
    impl->webview->ExecuteScript(L"window.dispatchEvent(new Event('mirdainforwardaccount'))", nullptr);
}

void SlicerWindow::showPage(const QUrl &page)
{
    load(page);
    show();
}

void SlicerWindow::openFiles(const QStringList &paths)
{
    if (paths.isEmpty()) return;
    impl->openedFiles.append(paths);
    showSlicer();
    if (impl->webview) impl->webview->ExecuteScript(L"window.dispatchEvent(new Event('mirdainforwardopen'))", nullptr);
}

// Stub: Windows has no drop panel yet, so models sent to Gwaith open normally.
void SlicerWindow::sendToGwaith(const QStringList &paths, const QJsonObject &) { openFiles(paths); }

void SlicerWindow::show()
{
    if (impl->current.isEmpty()) impl->current = url;
    if (!impl->started) impl->start();
    impl->window.showNormal();
    impl->window.raise();
    impl->window.activateWindow();
}
