#include "platform/SlicerWindow.h"

#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QWidget>

#include <WebView2.h>
#include <wrl.h>

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

// A plain Qt window whose native HWND hosts the WebView2 controller. It has no caption:
// the page's header is the title bar, with its own caption buttons.
class HostWidget : public QWidget {
public:
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;

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

protected:
    bool event(QEvent *event) override
    {
        const auto handled = QWidget::event(event);
        if (!controller) return handled;
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) fit();
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
    ComPtr<ICoreWebView2> webview;
    bool started = false;

    void start();
    void configure();
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
                    QDesktopServices::openUrl(url);
                    return S_OK;
                }
                return environment->CreateCoreWebView2Controller(reinterpret_cast<HWND>(window.winId()),
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT result, ICoreWebView2Controller *controller) -> HRESULT {
                            if (FAILED(result)) return result;
                            window.controller = controller;
                            controller->get_CoreWebView2(&webview);
                            window.webview = webview;
                            window.fit();
                            configure();
                            return webview->Navigate(wide(url.toString()).c_str());
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
            if ((target.scheme() == "http" || target.scheme() == "https") && !isSlicerHost(url, target.host())) {
                args->put_Cancel(TRUE);
                QDesktopServices::openUrl(target);
            }
            return S_OK;
        }).Get(), &token);

    // window.open and target=_blank: slicer pages replace this one, everything else opens in the browser.
    webview->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
        [this](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
            LPWSTR uri;
            args->get_Uri(&uri);
            const QUrl target(takeString(uri));
            args->put_Handled(TRUE);
            if (isSlicerHost(url, target.host())) webview->Navigate(wide(target.toString()).c_str());
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
            if (!isSlicerHost(url, QUrl(takeString(source)).host())) {
                reply(403, {{"error", "Forward is not available to this page."}});
                return S_OK;
            }
            const auto method = message.value("method").toString();
            // The header's caption buttons.
            if (method == "minimize" || method == "toggleMaximize" || method == "close") {
                if (method == "minimize") window.showMinimized();
                else if (method == "close") window.close();
                else if (window.isMaximized()) window.showNormal();
                else window.showMaximized();
                reply(200, {});
                return S_OK;
            }
            handler(method, message.value("body").toObject(), reply);
            return S_OK;
        }).Get(), &token);

    // File pickers and downloads (to the user's Downloads folder) use WebView2's built-in UI.
}

SlicerWindow::SlicerWindow(const QUrl &url, Handler handler) : url(url), impl(new Impl(this, url, std::move(handler))) {}

SlicerWindow::~SlicerWindow() = default;

void SlicerWindow::load(const QUrl &target)
{
    impl->webview->Navigate(wide(target.toString(QUrl::FullyEncoded)).c_str());
}

void SlicerWindow::show()
{
    if (!impl->started) impl->start();
    impl->window.showNormal();
    impl->window.raise();
    impl->window.activateWindow();
}
