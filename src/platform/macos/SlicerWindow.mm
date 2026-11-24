#include "platform/SlicerWindow.h"

#include <QJsonDocument>

#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

@class SlicerController;

struct SlicerWindow::Impl {
    SlicerWindow *owner;
    QUrl url;
    Handler handler;
    SlicerController *controller = nil;
};

static void openExternally(NSURL *url)
{
    if (url) [[NSWorkspace sharedWorkspace] openURL:url];
}

@interface SlicerController : NSObject <NSWindowDelegate, WKNavigationDelegate, WKUIDelegate,
    WKDownloadDelegate, WKScriptMessageHandlerWithReply>
@end

// The slicer's header is this tall; the traffic lights are centered in it.
static constexpr CGFloat HeaderHeight = 51;

@implementation SlicerController {
    SlicerWindow::Impl *_impl;
    NSWindow *_window;
    WKWebView *_webView;
    NSMenu *_previousMenu;
    NSMenu *_editMenu;
    NSEvent *_mouseDown;
    id _mouseMonitor;
}

- (instancetype)initWithImpl:(SlicerWindow::Impl *)impl
{
    self = [super init];
    _impl = impl;
    return self;
}

- (bool)isSlicerHost:(NSString *)host
{
    return host && isSlicerHost(_impl->url, QString::fromNSString(host));
}

- (void)show
{
    if (!_window) [self createWindow];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)createWindow
{
    auto *configuration = [[WKWebViewConfiguration alloc] init];
    configuration.websiteDataStore = [WKWebsiteDataStore defaultDataStore];
    [configuration.userContentController addScriptMessageHandlerWithReply:self
        contentWorld:[WKContentWorld pageWorld] name:@"mirdainForward"];
    // Marks the page as Forward's slicer window; the slicer checks it before using the bridge
    // and insets its header's left side for the traffic lights.
    [configuration.userContentController addUserScript:[[WKUserScript alloc]
        initWithSource:@"window.mirdainForward = { titleBar: 'left' }"
        injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:YES]];

    _webView = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:configuration];
    _webView.navigationDelegate = self;
    _webView.UIDelegate = self;
    _webView.allowsBackForwardNavigationGestures = YES;

    const auto screen = NSScreen.mainScreen.visibleFrame;
    const auto size = NSMakeSize(MIN(1440, screen.size.width * 0.9), MIN(900, screen.size.height * 0.9));
    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, size.width, size.height)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
            | NSWindowStyleMaskResizable | NSWindowStyleMaskFullSizeContentView
        backing:NSBackingStoreBuffered defer:NO];
    _window.title = @"Mirdain Slicer";
    // The page's header is the title bar: the webview fills the window under the traffic lights.
    _window.titlebarAppearsTransparent = YES;
    _window.titleVisibility = NSWindowTitleHidden;
    _window.releasedWhenClosed = NO;
    _window.contentMinSize = NSMakeSize(800, 560);
    _window.contentView = _webView;
    _window.delegate = self;
    [_window center];
    [self positionTrafficLights];
    [_webView loadRequest:[NSURLRequest requestWithURL:_impl->url.toNSURL()]];

    // The page asks to drag from its header after a mouse down, so keep the event to drag with.
    __weak NSWindow *window = _window;
    __weak SlicerController *weakSelf = self;
    _mouseMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown handler:^NSEvent *(NSEvent *event) {
        SlicerController *controller = weakSelf;
        if (controller && event.window == window) controller->_mouseDown = event;
        return event;
    }];
}

// AppKit lays the title bar out at its standard height, so grow it to the header's height
// and center the buttons in it whenever the window lays out again.
- (void)positionTrafficLights
{
    NSButton *close = [_window standardWindowButton:NSWindowCloseButton];
    NSView *titlebar = close.superview.superview;
    if (!titlebar || _window.styleMask & NSWindowStyleMaskFullScreen) return;
    auto frame = titlebar.frame;
    frame.size.height = HeaderHeight;
    frame.origin.y = _window.frame.size.height - HeaderHeight;
    titlebar.frame = frame;
    CGFloat x = 18;
    for (auto type : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
        NSButton *button = [_window standardWindowButton:type];
        [button setFrameOrigin:NSMakePoint(x, (HeaderHeight - button.frame.size.height) / 2)];
        x += 20;
    }
}

- (void)windowDidResize:(NSNotification *)notification { [self positionTrafficLights]; }

- (void)setFullScreen:(bool)fullScreen
{
    // Full screen hides the traffic lights, so the header no longer needs its inset.
    [_webView evaluateJavaScript:fullScreen ? @"document.documentElement.dataset.forwardFullScreen = ''"
                                            : @"delete document.documentElement.dataset.forwardFullScreen"
        completionHandler:nil];
}

- (void)windowWillEnterFullScreen:(NSNotification *)notification { [self setFullScreen:true]; }

- (void)windowDidExitFullScreen:(NSNotification *)notification
{
    [self setFullScreen:false];
    [self positionTrafficLights];
}

- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation
{
    if (_window.styleMask & NSWindowStyleMaskFullScreen) [self setFullScreen:true];
}

- (void)load:(NSURL *)url
{
    [_webView loadRequest:[NSURLRequest requestWithURL:url]];
}

// MARK: Edit menu

// Forward is an LSUIElement app with no menu bar of its own, so the standard editing
// shortcuts only reach the webview while this menu is installed.
- (NSMenu *)editMenu
{
    if (_editMenu) return _editMenu;
    _editMenu = [[NSMenu alloc] init];
    auto *appItem = [_editMenu addItemWithTitle:@"" action:nil keyEquivalent:@""];
    auto *appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"Hide Mirdain Slicer" action:@selector(hide:) keyEquivalent:@"h"];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"];
    appItem.submenu = appMenu;

    auto *editItem = [_editMenu addItemWithTitle:@"Edit" action:nil keyEquivalent:@""];
    auto *edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    [edit addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
    [[edit addItemWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"z"]
        setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagShift];
    [edit addItem:[NSMenuItem separatorItem]];
    [edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    editItem.submenu = edit;
    return _editMenu;
}

- (void)windowDidBecomeKey:(NSNotification *)notification
{
    [self positionTrafficLights];
    if (NSApp.mainMenu != self.editMenu) _previousMenu = NSApp.mainMenu;
    NSApp.mainMenu = self.editMenu;
}

- (void)windowDidResignKey:(NSNotification *)notification
{
    if (NSApp.mainMenu == _editMenu) NSApp.mainMenu = _previousMenu;
}

- (void)windowWillClose:(NSNotification *)notification
{
    [self windowDidResignKey:notification];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

// MARK: Navigation

- (void)webView:(WKWebView *)webView decidePolicyForNavigationAction:(WKNavigationAction *)action
    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler
{
    if (action.shouldPerformDownload) {
        decisionHandler(WKNavigationActionPolicyDownload);
        return;
    }
    NSURL *url = action.request.URL;
    if (action.targetFrame.isMainFrame && _impl->owner->openSignInExternally(QUrl::fromNSURL(url))) {
        decisionHandler(WKNavigationActionPolicyCancel);
        return;
    }
    const bool web = [url.scheme isEqualToString:@"http"] || [url.scheme isEqualToString:@"https"];
    if (action.targetFrame.isMainFrame && web && ![self isSlicerHost:url.host]) {
        openExternally(url);
        decisionHandler(WKNavigationActionPolicyCancel);
        return;
    }
    decisionHandler(WKNavigationActionPolicyAllow);
}

- (void)webView:(WKWebView *)webView decidePolicyForNavigationResponse:(WKNavigationResponse *)response
    decisionHandler:(void (^)(WKNavigationResponsePolicy))decisionHandler
{
    auto *http = [response.response isKindOfClass:[NSHTTPURLResponse class]] ? (NSHTTPURLResponse *)response.response : nil;
    NSString *disposition = [http valueForHTTPHeaderField:@"Content-Disposition"];
    const bool attachment = [disposition.lowercaseString hasPrefix:@"attachment"];
    decisionHandler(attachment || !response.canShowMIMEType ? WKNavigationResponsePolicyDownload
                                                            : WKNavigationResponsePolicyAllow);
}

- (WKWebView *)webView:(WKWebView *)webView createWebViewWithConfiguration:(WKWebViewConfiguration *)configuration
    forNavigationAction:(WKNavigationAction *)action windowFeatures:(WKWindowFeatures *)features
{
    // window.open and target=_blank: slicer pages replace this one, everything else opens in the browser.
    NSURL *url = action.request.URL;
    if ([self isSlicerHost:url.host]) [webView loadRequest:action.request];
    else openExternally(url);
    return nil;
}

// MARK: Files

- (void)webView:(WKWebView *)webView runOpenPanelWithParameters:(WKOpenPanelParameters *)parameters
    initiatedByFrame:(WKFrameInfo *)frame completionHandler:(void (^)(NSArray<NSURL *> *))completionHandler
{
    auto *panel = [NSOpenPanel openPanel];
    panel.allowsMultipleSelection = parameters.allowsMultipleSelection;
    panel.canChooseDirectories = parameters.allowsDirectories;
    panel.canChooseFiles = YES;
    [panel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse result) {
        completionHandler(result == NSModalResponseOK ? panel.URLs : nil);
    }];
}

- (void)webView:(WKWebView *)webView navigationAction:(WKNavigationAction *)action didBecomeDownload:(WKDownload *)download
{
    download.delegate = self;
}

- (void)webView:(WKWebView *)webView navigationResponse:(WKNavigationResponse *)response didBecomeDownload:(WKDownload *)download
{
    download.delegate = self;
}

- (void)download:(WKDownload *)download decideDestinationUsingResponse:(NSURLResponse *)response
    suggestedFilename:(NSString *)filename completionHandler:(void (^)(NSURL *))completionHandler
{
    NSURL *folder = [NSFileManager.defaultManager URLsForDirectory:NSDownloadsDirectory inDomains:NSUserDomainMask].firstObject;
    NSString *name = filename.lastPathComponent.length ? filename.lastPathComponent : @"download";
    NSURL *destination = [folder URLByAppendingPathComponent:name];
    for (int copy = 2; [NSFileManager.defaultManager fileExistsAtPath:destination.path]; ++copy) {
        NSString *extension = name.pathExtension;
        NSString *base = [NSString stringWithFormat:@"%@ %d", name.stringByDeletingPathExtension, copy];
        destination = [folder URLByAppendingPathComponent:extension.length ? [base stringByAppendingPathExtension:extension] : base];
    }
    completionHandler(destination);
}

- (void)download:(WKDownload *)download didFailWithError:(NSError *)error resumeData:(NSData *)resumeData
{
    [[NSAlert alertWithError:error] beginSheetModalForWindow:_window completionHandler:nil];
}

// MARK: Bridge

- (void)userContentController:(WKUserContentController *)controller didReceiveScriptMessage:(WKScriptMessage *)message
    replyHandler:(void (^)(id, NSString *))replyHandler
{
    if (![self isSlicerHost:message.frameInfo.securityOrigin.host] || ![message.body isKindOfClass:[NSDictionary class]]) {
        replyHandler(nil, @"Forward is not available to this page.");
        return;
    }
    NSDictionary *payload = message.body;
    NSString *method = [payload[@"method"] description];
    // Window controls for the page's header, which replaces the title bar.
    if ([method isEqualToString:@"startDrag"]) {
        if (_mouseDown && NSEvent.pressedMouseButtons & 1) [_window performWindowDragWithEvent:_mouseDown];
        replyHandler(@{@"status": @200, @"body": @{}}, nil);
        return;
    }
    if ([method isEqualToString:@"titleBarDoubleClick"]) {
        NSString *action = [NSUserDefaults.standardUserDefaults stringForKey:@"AppleActionOnDoubleClick"];
        if ([action isEqualToString:@"Minimize"]) [_window performMiniaturize:nil];
        else if (![action isEqualToString:@"None"]) [_window performZoom:nil];
        replyHandler(@{@"status": @200, @"body": @{}}, nil);
        return;
    }
    id body = payload[@"body"] ?: @{};
    NSData *json = [NSJSONSerialization dataWithJSONObject:body options:0 error:nil];
    const auto document = QJsonDocument::fromJson(QByteArray::fromNSData(json));
    auto reply = [replyHandler](int status, const QJsonObject &result) {
        NSData *data = QJsonDocument(result).toJson(QJsonDocument::Compact).toNSData();
        replyHandler(@{@"status": @(status), @"body": [NSJSONSerialization JSONObjectWithData:data options:0 error:nil]}, nil);
    };
    _impl->handler(QString::fromNSString(method), document.object(), reply);
}

@end

SlicerWindow::SlicerWindow(const QUrl &url, Handler handler) : url(url), impl(new Impl{this, url, std::move(handler)})
{
    impl->controller = [[SlicerController alloc] initWithImpl:impl.get()];
}

SlicerWindow::~SlicerWindow() = default;

void SlicerWindow::show() { [impl->controller show]; }

void SlicerWindow::load(const QUrl &target) { [impl->controller load:target.toNSURL()]; }
