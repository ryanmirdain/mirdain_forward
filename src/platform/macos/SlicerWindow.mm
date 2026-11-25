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
    NSURL *_requestedURL;
    NSMenu *_previousMenu;
    NSMenu *_editMenu;
    NSEvent *_mouseDown;
    id _mouseMonitor;
    NSMutableArray<NSURL *> *_openedFiles;
    // Drops on Forward's drop panel, for the page to send to Gwaith: each has urls and the
    // printer they were dropped on, as the JSON argument of mirdainForwardOpen.
    NSMutableArray<NSDictionary *> *_gwaithDrops;
    // The queue that answers the picker opened by mirdainForwardOpen, while it runs.
    NSMutableArray<NSURL *> *_answering;
    // Which menu bar items the page has enabled, reported with the menuBar bridge method.
    bool _fileMenuEnabled;
    bool _libraryAvailable;
    bool _reportBugAvailable;
}

- (instancetype)initWithImpl:(SlicerWindow::Impl *)impl
{
    self = [super init];
    _impl = impl;
    _openedFiles = [NSMutableArray array];
    _gwaithDrops = [NSMutableArray array];
    return self;
}

- (bool)allows:(NSURL *)url
{
    return url && isWindowPage(_impl->url, QUrl::fromNSURL(url));
}

- (bool)showingSlicer
{
    // The latest main-frame navigation; the web view's own URL lags behind one that just started.
    return _requestedURL && isSlicerHost(_impl->url, QString::fromNSString(_requestedURL.host));
}

- (void)show
{
    if (!_window) [self createWindow:_impl->url.toNSURL()];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)showURL:(NSURL *)url
{
    if (_window) [self load:url];
    else [self createWindow:url];
    [self show];
}

- (void)createWindow:(NSURL *)url
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
    [configuration.userContentController addUserScript:[[WKUserScript alloc]
        initWithSource:@"(() => { const send = (kind, args) => window.webkit.messageHandlers.mirdainForward.postMessage({ method: 'debugLog', body: { kind, text: args.map(a => a instanceof Error ? a.stack || a.message : typeof a === 'object' ? (() => { try { return JSON.stringify(a) } catch { return String(a) } })() : String(a)).join(' ') } }).catch(() => {});"
            "for (const kind of ['error', 'warn', 'log', 'info']) { const original = console[kind]; console[kind] = (...args) => { send(kind, args); original.apply(console, args) } }"
            "addEventListener('error', e => send('onerror', [e.message, e.filename + ':' + e.lineno]));"
            "addEventListener('unhandledrejection', e => send('rejection', [e.reason]));"
            "document.addEventListener('change', e => send('change', [e.target.tagName, e.target.accept, e.target.files && e.target.files.length]), true);"
            "const fetch0 = window.fetch; window.fetch = async (...a) => { const url = String(a[0] && a[0].url || a[0]); try { const r = await fetch0(...a); send('fetch', [a[1] && a[1].method || 'GET', url.slice(0, 120), r.status]); return r } catch (e) { send('fetch', ['FAILED', url.slice(0, 120), String(e)]); throw e } };"
            "const xopen = XMLHttpRequest.prototype.open; XMLHttpRequest.prototype.open = function (m, u, ...r) { this.addEventListener('loadend', () => send('xhr', [m, String(u).slice(0, 120), this.status])); return xopen.call(this, m, u, ...r) } })()"
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
    _requestedURL = url;
    [_webView loadRequest:[NSURLRequest requestWithURL:url]];

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

- (void)openFiles:(NSArray<NSURL *> *)urls gwaithPrinter:(NSString *)printer
{
    NSLog(@"DEBUGOPEN openFiles %@ showingSlicer=%d requested=%@", urls, [self showingSlicer], _requestedURL);
    if (printer) [_gwaithDrops addObject:@{@"urls": [urls mutableCopy], @"printer": printer}];
    else [_openedFiles addObjectsFromArray:urls];
    _impl->owner->showSlicer();
    [_webView evaluateJavaScript:@"window.dispatchEvent(new Event('mirdainforwardopen'))" completionHandler:nil];
}

// Opening a file input's picker needs a user gesture, which script run from here carries.
// The page's mirdainForwardOpen clicks its model input and returns true; the picker request
// that click sends arrives before the script's result, and is answered with the queued files.
// Each call delivers one Gwaith drop, oldest first, or else the opened models; the page asks
// again for each mirdainforwardopen.
- (void)deliverOpenedFiles
{
    NSDictionary *drop = _gwaithDrops.firstObject;
    NSLog(@"DEBUGOPEN deliverOpenedFiles queued=%lu", (unsigned long)_openedFiles.count);
    if (!drop && !_openedFiles.count) return;
    _answering = drop ? drop[@"urls"] : _openedFiles;
    NSString *script = drop ? [NSString stringWithFormat:@"window.mirdainForwardOpen?.('gwaith', %@) === true", drop[@"printer"]]
                            : @"window.mirdainForwardOpen?.('model') === true";
    [_webView evaluateJavaScript:script completionHandler:^(id result, NSError *error) {
        NSLog(@"DEBUGOPEN pick result=%@ error=%@", result, error);
        self->_answering = nil;
        if (drop && ![drop[@"urls"] count]) [self->_gwaithDrops removeObject:drop];
    }];
}

- (void)dispatchEvent:(NSString *)name
{
    NSString *script = [NSString stringWithFormat:@"window.dispatchEvent(new Event('%@'))", name];
    [_webView evaluateJavaScript:script completionHandler:nil];
}

- (void)load:(NSURL *)url
{
    _requestedURL = url;
    [_webView loadRequest:[NSURLRequest requestWithURL:url]];
}

// MARK: Menu bar

// Forward is an LSUIElement app with no menu bar of its own, so this one is installed while
// the slicer window is key: the page's File menu, the standard editing shortcuts, and Help.
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

    // Mirrors the page's File menu and Report a bug button; each item runs the page's own action.
    auto *fileItem = [_editMenu addItemWithTitle:@"File" action:nil keyEquivalent:@""];
    auto *file = [[NSMenu alloc] initWithTitle:@"File"];
    [file addItemWithTitle:@"Open .mirdain" action:@selector(openArchive:) keyEquivalent:@"o"].target = self;
    [file addItemWithTitle:@"Open from library" action:@selector(openLibrary:) keyEquivalent:@""].target = self;
    [file addItemWithTitle:@"New" action:@selector(newProject:) keyEquivalent:@"n"].target = self;
    fileItem.submenu = file;

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

    auto *helpItem = [_editMenu addItemWithTitle:@"Help" action:nil keyEquivalent:@""];
    auto *help = [[NSMenu alloc] initWithTitle:@"Help"];
    [help addItemWithTitle:@"Report a bug" action:@selector(reportBug:) keyEquivalent:@""].target = self;
    helpItem.submenu = help;
    // Gives the Help menu its standard search field.
    NSApp.helpMenu = help;
    return _editMenu;
}

- (void)runMenuAction:(NSString *)action
{
    // Script run from here carries a user gesture, so Open .mirdain can open the file picker.
    NSString *script = [NSString stringWithFormat:@"window.mirdainForwardMenu?.('%@')", action];
    [_webView evaluateJavaScript:script completionHandler:nil];
}

- (void)openArchive:(id)sender { [self runMenuAction:@"open"]; }
- (void)openLibrary:(id)sender { [self runMenuAction:@"library"]; }
- (void)newProject:(id)sender { [self runMenuAction:@"new"]; }
- (void)reportBug:(id)sender { [self runMenuAction:@"reportBug"]; }

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    if (item.action == @selector(reportBug:)) return _reportBugAvailable;
    if (item.action == @selector(openLibrary:)) return _libraryAvailable;
    if (item.action == @selector(openArchive:) || item.action == @selector(newProject:)) return _fileMenuEnabled;
    return YES;
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
    if (action.targetFrame.isMainFrame && web && ![self allows:url]) {
        openExternally(url);
        decisionHandler(WKNavigationActionPolicyCancel);
        return;
    }
    if (action.targetFrame.isMainFrame) _requestedURL = url;
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
    if ([self allows:url]) [webView loadRequest:action.request];
    else openExternally(url);
    return nil;
}

// MARK: Files

- (void)webView:(WKWebView *)webView runOpenPanelWithParameters:(WKOpenPanelParameters *)parameters
    initiatedByFrame:(WKFrameInfo *)frame completionHandler:(void (^)(NSArray<NSURL *> *))completionHandler
{
    NSLog(@"DEBUGOPEN open panel answering=%lu", (unsigned long)_answering.count);
    if (_answering.count) {
        NSMutableArray<NSURL *> *queue = _answering;
        _answering = nil;
        NSArray<NSURL *> *files = parameters.allowsMultipleSelection ? [queue copy] : @[queue.firstObject];
        [queue removeObjectsInArray:files];
        completionHandler(files);
        return;
    }
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
    WKSecurityOrigin *origin = message.frameInfo.securityOrigin;
    QUrl page;
    page.setScheme(QString::fromNSString(origin.protocol));
    page.setHost(QString::fromNSString(origin.host));
    if (origin.port) page.setPort(int(origin.port));
    if (!isWindowPage(_impl->url, page) || ![message.body isKindOfClass:[NSDictionary class]]) {
        replyHandler(nil, @"Forward is not available to this page.");
        return;
    }
    NSDictionary *payload = message.body;
    NSString *method = [payload[@"method"] description];
    if ([method isEqualToString:@"debugLog"]) {
        NSLog(@"DEBUGOPEN page %@: %@", payload[@"body"][@"kind"], payload[@"body"][@"text"]);
        replyHandler(@{@"status": @200, @"body": @{}}, nil);
        return;
    }
    NSLog(@"DEBUGOPEN bridge %@ from %@", method, page.toString().toNSString());
    // Window controls for the page's header, which replaces the title bar.
    if ([method isEqualToString:@"startDrag"]) {
        if (_mouseDown && NSEvent.pressedMouseButtons & 1) [_window performWindowDragWithEvent:_mouseDown];
        replyHandler(@{@"status": @200, @"body": @{}}, nil);
        return;
    }
    if ([method isEqualToString:@"menuBar"]) {
        NSDictionary *body = [payload[@"body"] isKindOfClass:[NSDictionary class]] ? payload[@"body"] : @{};
        _fileMenuEnabled = [body[@"file"] boolValue];
        _libraryAvailable = [body[@"library"] boolValue];
        _reportBugAvailable = [body[@"reportBug"] boolValue];
        replyHandler(@{@"status": @200, @"body": @{}}, nil);
        return;
    }
    if ([method isEqualToString:@"requestOpenedFiles"]) {
        [self deliverOpenedFiles];
        replyHandler(@{@"status": @200, @"body": @{}}, nil);
        return;
    }
    // Closes the window, as from Forward's Preferences page; Forward keeps running in the menu bar.
    if ([method isEqualToString:@"close"]) {
        [_window performClose:nil];
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

void SlicerWindow::showPage(const QUrl &page) { [impl->controller showURL:page.toNSURL()]; }

bool SlicerWindow::showingSlicer() const { return [impl->controller showingSlicer]; }

void SlicerWindow::setSlicerUrl(const QUrl &slicer)
{
    if (slicer == url) return;
    url = slicer;
    impl->url = slicer;
    if (showingSlicer()) load(url);
}

void SlicerWindow::accountChanged() { [impl->controller dispatchEvent:@"mirdainforwardaccount"]; }

void SlicerWindow::load(const QUrl &target) { [impl->controller load:target.toNSURL()]; }

void SlicerWindow::openFiles(const QStringList &paths)
{
    if (paths.isEmpty()) return;
    auto *urls = [NSMutableArray<NSURL *> array];
    for (const auto &path : paths) [urls addObject:[NSURL fileURLWithPath:path.toNSString()]];
    [impl->controller openFiles:urls gwaithPrinter:nil];
}

void SlicerWindow::sendToGwaith(const QStringList &paths, const QJsonObject &printer)
{
    if (paths.isEmpty()) return;
    auto *urls = [NSMutableArray<NSURL *> array];
    for (const auto &path : paths) [urls addObject:[NSURL fileURLWithPath:path.toNSString()]];
    [impl->controller openFiles:urls gwaithPrinter:QString::fromUtf8(QJsonDocument(printer).toJson(QJsonDocument::Compact)).toNSString()];
}
