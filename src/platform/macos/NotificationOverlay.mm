#include "platform/NotificationOverlay.h"
#include "platform/macos/MirdainStyle.h"

#include <QSystemTrayIcon>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <set>

static constexpr CGFloat CardWidth = 300, Padding = 12, IconSize = 16, TextInset = Padding + IconSize + 10, CloseSize = 22;

// Text and icons in a row pass clicks through to it, so clicking anywhere on the row works.
@interface RowLabel : NSTextField
@end

@implementation RowLabel
- (NSView *)hitTest:(NSPoint)point { return nil; }
@end

@interface RowIcon : NSImageView
@end

@implementation RowIcon
- (NSView *)hitTest:(NSPoint)point { return nil; }
@end

// lines is the most lines to show, or 0 for all of them.
static NSTextField *label(NSString *text, NSFont *font, NSColor *color, CGFloat width, NSInteger lines)
{
    auto *field = lines == 1 ? [RowLabel labelWithString:text] : [RowLabel wrappingLabelWithString:text];
    // Wrapping labels are selectable by default, which would take clicks meant for the row.
    field.selectable = NO;
    field.font = font;
    field.textColor = color;
    field.maximumNumberOfLines = lines;
    // Truncating mid-line turns wrapping off, so a multi-line label wraps and only cuts its
    // last visible line short.
    field.lineBreakMode = lines == 1 ? NSLineBreakByTruncatingTail : NSLineBreakByWordWrapping;
    field.cell.truncatesLastVisibleLine = YES;
    field.preferredMaxLayoutWidth = width;
    field.frame = NSMakeRect(0, 0, width, field.fittingSize.height);
    return field;
}

static NSImageView *symbol(NSString *name, NSColor *color)
{
    auto *image = [RowIcon imageViewWithImage:[NSImage imageWithSystemSymbolName:name accessibilityDescription:nil]];
    image.contentTintColor = color;
    image.frame = NSMakeRect(0, 0, IconSize, IconSize);
    return image;
}

// The working indicator: a ring whose comet tail runs from transparent through purple to
// Mirdain pink, spinning. Every spinner takes its angle from the system clock, so one rebuilt
// with its row keeps turning from where the last left off.
@interface MirdainSpinner : NSView
@end

@implementation MirdainSpinner

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    self.wantsLayer = YES;
    auto *ring = [CAShapeLayer layer];
    ring.frame = self.bounds;
    auto circle = CGPathCreateWithEllipseInRect(NSInsetRect(self.bounds, 1.5, 1.5), nullptr);
    ring.path = circle;
    CGPathRelease(circle);
    ring.fillColor = NSColor.clearColor.CGColor;
    ring.strokeColor = NSColor.blackColor.CGColor;
    ring.lineWidth = 2;
    ring.lineCap = kCALineCapRound;
    auto *gradient = [CAGradientLayer layer];
    gradient.frame = self.bounds;
    gradient.type = kCAGradientLayerConic;
    gradient.startPoint = CGPointMake(0.5, 0.5);
    gradient.endPoint = CGPointMake(0.5, 1);
    gradient.colors = @[(id)mirdainColor(0xa46ff2, 0).CGColor, (id)mirdainColor(0xa46ff2).CGColor, (id)mirdainAccent().CGColor];
    gradient.mask = ring;
    auto *spin = [CABasicAnimation animationWithKeyPath:@"transform.rotation.z"];
    spin.fromValue = @0;
    spin.toValue = @(-2 * M_PI);
    spin.duration = 0.9;
    spin.repeatCount = HUGE_VALF;
    spin.timeOffset = fmod(CACurrentMediaTime(), spin.duration);
    [gradient addAnimation:spin forKey:@"spin"];
    [self.layer addSublayer:gradient];
    return self;
}

@end

// Clicks act at once, without first activating Forward. Handlers run once the click is done,
// since they rebuild the card this button sits in.
@interface NotificationClose : NSButton
@property (copy) void (^dismissed)(void);
@end

@implementation NotificationClose
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (void)clicked:(id)sender { dispatch_async(dispatch_get_main_queue(), _dismissed); }
@end

// One notification. Clicking it, outside its close button, activates it.
@interface NotificationRow : NSView
// Nil when the row isn't clickable: no hover, no hand cursor, and clicks do nothing.
@property (copy) void (^opened)(void);
@property (nonatomic) BOOL hovered;
@end

@implementation NotificationRow {
    BOOL _pressed;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }

- (void)mouseDown:(NSEvent *)event
{
    if (!_opened) return;
    _pressed = YES;
    self.needsDisplay = YES;
}

- (void)mouseUp:(NSEvent *)event
{
    _pressed = NO;
    self.needsDisplay = YES;
    if (_opened && NSPointInRect([self convertPoint:event.locationInWindow fromView:nil], self.bounds))
        dispatch_async(dispatch_get_main_queue(), _opened);
}

// The panel never activates Forward, so cursor rects and cursor updates don't apply: the row
// sets the cursor itself as the mouse enters and moves over it.
- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    for (NSTrackingArea *area in self.trackingAreas) [self removeTrackingArea:area];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect
        options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect
        owner:self userInfo:nil]];
}

// Rows are rebuilt on every update: one appearing under the mouse starts hovered.
- (void)viewDidMoveToWindow
{
    if (!self.window) return;
    const auto mouse = [self convertPoint:self.window.mouseLocationOutsideOfEventStream fromView:nil];
    if (NSPointInRect(mouse, self.bounds)) [self hover];
}

- (void)hover
{
    if (!_opened) return;
    self.hovered = YES;
    [NSCursor.pointingHandCursor set];
}

- (void)mouseEntered:(NSEvent *)event { [self hover]; }

- (void)mouseMoved:(NSEvent *)event { if (_opened) [NSCursor.pointingHandCursor set]; }

- (void)mouseExited:(NSEvent *)event
{
    if (!_opened) return;
    self.hovered = NO;
    _pressed = NO;
    [NSCursor.arrowCursor set];
}

- (void)setHovered:(BOOL)hovered
{
    _hovered = hovered;
    self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirtyRect
{
    if (!_hovered) return;
    // Raised while hovered, tinted pink while pressed; inset from the card's border on each side.
    [(_pressed ? mirdainColor(0xe967c8, 0.14) : mirdainColor(0x172429)) setFill];
    NSRectFill(NSInsetRect(self.bounds, 1, 0));
}

@end

// A thin bar filled to progress, in the spinner's purple-to-pink.
static NSView *progressBar(double progress, CGFloat width)
{
    auto *track = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, 3)];
    track.wantsLayer = YES;
    track.layer.backgroundColor = mirdainLine().CGColor;
    track.layer.cornerRadius = 1.5;
    track.layer.masksToBounds = YES;
    auto *fill = [CAGradientLayer layer];
    fill.frame = CGRectMake(0, 0, width * std::clamp(progress, 0.0, 1.0), 3);
    fill.startPoint = CGPointMake(0, 0.5);
    fill.endPoint = CGPointMake(1, 0.5);
    fill.colors = @[(id)mirdainColor(0xa46ff2).CGColor, (id)mirdainAccent().CGColor];
    [track.layer addSublayer:fill];
    return track;
}

struct NotificationOverlay::Impl {
    QSystemTrayIcon &tray;
    std::function<void(const QString &)> activate;
    std::function<void(const QString &)> dismiss;
    NSPanel *panel = nil;
    bool shown = false;
    // The latest notifications, kept while the card is hidden for a full-screen slicer.
    QList<Item> items;
    bool covered = false;
    // Failed notifications showing their whole error, by ID.
    std::set<QString> expanded;
    NSArray *observers = nil;

    NSView *layout(const QList<Item> &items);
    NSView *row(const Item &item);
    void update();
    void hide();
};

// The slicer window full screen on the Space in view: the card would sit over its header. It's
// Forward's only window that goes full screen.
static bool fullScreenSlicerInView()
{
    for (NSWindow *window in NSApp.windows)
        if ((window.styleMask & NSWindowStyleMaskFullScreen) && window.isOnActiveSpace) return true;
    return false;
}

NSView *NotificationOverlay::Impl::row(const Item &item)
{
    // Rows with no icon (none, or a progress bar beneath) start their text at the edge.
    const CGFloat inset = item.indicator == Indicator::None || item.indicator == Indicator::Progress ? Padding : TextInset;
    const CGFloat textWidth = CardWidth - inset - Padding;
    // A failed notification with nothing to open shows two lines of its error until clicked, then
    // all of it. One that can be opened (a Gwaith run with its session) opens instead.
    const bool failed = item.indicator == Indicator::Failed;
    const bool expandable = failed && !item.clickable;
    const bool whole = expandable && expanded.count(item.id);
    auto *title = label(item.title.toNSString(), aileron(true, 13), mirdainText(), textWidth - CloseSize, 1);
    auto *body = label(item.body.toNSString(), aileron(false, 12),
        failed ? mirdainColor(0xf2a07a) : mirdainMuted(), textWidth, whole ? 0 : 2);
    [title setFrameOrigin:NSMakePoint(inset, Padding)];
    [body setFrameOrigin:NSMakePoint(inset, NSMaxY(title.frame) + 2)];
    CGFloat bottom = item.body.isEmpty() ? NSMaxY(title.frame) : NSMaxY(body.frame);
    NSView *bar = nil;
    if (item.indicator == Indicator::Progress) {
        bar = progressBar(item.progress, textWidth);
        [bar setFrameOrigin:NSMakePoint(inset, bottom + 8)];
        bottom = NSMaxY(bar.frame);
    }

    auto *view = [[NotificationRow alloc] initWithFrame:NSMakeRect(0, 0, CardWidth, bottom + Padding)];
    const auto id = item.id;
    auto activate = this->activate;
    if (expandable) {
        // Clicking it shows or hides the rest of its error.
        view.opened = ^{
            if (!expanded.erase(id)) expanded.insert(id);
            update();
        };
    } else if (item.clickable) {
        view.opened = ^{ activate(id); };
    }
    [view addSubview:title];
    if (!item.body.isEmpty()) [view addSubview:body];
    if (bar) [view addSubview:bar];

    NSView *icon = item.indicator == Indicator::Spinner ? [[MirdainSpinner alloc] initWithFrame:NSMakeRect(0, 0, IconSize, IconSize)]
        : item.indicator == Indicator::Done ? symbol(@"checkmark.circle.fill", mirdainAccent())
        : item.indicator == Indicator::Failed ? symbol(@"exclamationmark.triangle.fill", mirdainColor(0xf2a07a))
        : item.indicator == Indicator::Attention ? symbol(@"exclamationmark.circle.fill", mirdainAccent()) : nil;
    if (icon) {
        [icon setFrameOrigin:NSMakePoint(Padding, Padding + 1)];
        [view addSubview:icon];
    }

    auto *close = [NotificationClose buttonWithImage:[NSImage imageWithSystemSymbolName:@"xmark" accessibilityDescription:@"Dismiss"]
        target:nil action:@selector(clicked:)];
    close.target = close;
    close.bordered = NO;
    close.contentTintColor = mirdainMuted();
    close.frame = NSMakeRect(CardWidth - Padding - CloseSize + 4, Padding - 4, CloseSize, CloseSize);
    close.toolTip = @"Dismiss";
    auto dismiss = this->dismiss;
    close.dismissed = ^{ dismiss(id); };
    [view addSubview:close];
    return view;
}

NSView *NotificationOverlay::Impl::layout(const QList<Item> &items)
{
    NSMutableArray<NSView *> *rows = [NSMutableArray array];
    CGFloat height = 0;
    for (const auto &item : items) {
        NSView *view = row(item);
        [rows addObject:view];
        height += view.frame.size.height;
    }
    // Rows sit one point inside the card's top and bottom border, with a line between each.
    height += items.size() - 1 + 2;
    NSView *content;
    auto *root = mirdainCard(NSMakeSize(CardWidth, height), &content, true);
    // Rows clip to the card's rounded corners.
    content.wantsLayer = YES;
    content.layer.cornerRadius = 6;
    content.layer.masksToBounds = YES;
    CGFloat y = 1;
    for (NSView *view in rows) {
        if (y > 1) {
            auto *line = [[NSBox alloc] initWithFrame:NSMakeRect(1, y, CardWidth - 2, 1)];
            line.boxType = NSBoxCustom;
            line.borderWidth = 0;
            line.fillColor = mirdainLine();
            [content addSubview:line];
            y += 1;
        }
        [view setFrameOrigin:NSMakePoint(0, y)];
        [content addSubview:view];
        y += view.frame.size.height;
    }
    return root;
}

void NotificationOverlay::Impl::hide()
{
    if (!shown) return;
    shown = false;
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.14;
        context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseIn];
        panel.animator.alphaValue = 0;
    } completionHandler:^{
        if (!shown) [panel orderOut:nil];
    }];
}

NotificationOverlay::NotificationOverlay(QSystemTrayIcon &tray, std::function<void(const QString &)> activate,
    std::function<void(const QString &)> dismiss)
    : impl(new Impl{tray, std::move(activate), std::move(dismiss)})
{
    impl->panel = mirdainPanel();
    // Hidden while the slicer is full screen in view; shown again on leaving full screen or
    // switching to another Space.
    Impl *state = impl.get();
    auto recheck = ^(NSNotification *) {
        state->covered = fullScreenSlicerInView();
        state->update();
    };
    auto *center = NSNotificationCenter.defaultCenter;
    auto *workspace = NSWorkspace.sharedWorkspace.notificationCenter;
    impl->observers = @[
        [center addObserverForName:NSWindowDidEnterFullScreenNotification object:nil queue:nil usingBlock:recheck],
        [center addObserverForName:NSWindowDidExitFullScreenNotification object:nil queue:nil usingBlock:recheck],
        [workspace addObserverForName:NSWorkspaceActiveSpaceDidChangeNotification object:nil queue:nil usingBlock:recheck],
    ];
}

bool NotificationOverlay::available() { return true; }

NotificationOverlay::~NotificationOverlay()
{
    [NSNotificationCenter.defaultCenter removeObserver:impl->observers[0]];
    [NSNotificationCenter.defaultCenter removeObserver:impl->observers[1]];
    [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:impl->observers[2]];
    [impl->panel orderOut:nil];
}

void NotificationOverlay::show(const QList<Item> &items)
{
    impl->items = items;
    for (auto id = impl->expanded.begin(); id != impl->expanded.end();)
        if (std::none_of(items.begin(), items.end(), [&](const Item &item) { return item.id == *id; })) id = impl->expanded.erase(id);
        else ++id;
    impl->update();
}

void NotificationOverlay::Impl::update()
{
    if (items.isEmpty() || covered) {
        hide();
        return;
    }
    auto *content = layout(items);
    const auto frame = mirdainTrayPanelFrame(tray, content.frame.size);
    // The card only ever fades: it never moves, so updates arriving mid-animation can't leave it
    // out of place, and it takes over from the drop panel's notes card in the same spot.
    [panel setFrame:frame display:NO];
    panel.contentView = content;
    if (shown) return;
    shown = true;
    panel.alphaValue = 0;
    [panel orderFrontRegardless];
    NSPanel *card = panel;
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.18;
        context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
        card.animator.alphaValue = 1;
    }];
}
