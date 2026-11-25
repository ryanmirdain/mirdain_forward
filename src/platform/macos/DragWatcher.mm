#include "platform/DragWatcher.h"

#include <QFile>
#include <QSystemTrayIcon>

#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <QuartzCore/QuartzCore.h>

static NSArray<NSURL *> *modelURLs(NSPasteboard *pasteboard)
{
    NSArray<NSURL *> *urls = [pasteboard readObjectsForClasses:@[NSURL.class]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    NSMutableArray<NSURL *> *models = [NSMutableArray array];
    for (NSURL *url in urls) {
        NSString *extension = url.pathExtension.lowercaseString;
        if ([extension isEqualToString:@"stl"] || [extension isEqualToString:@"3mf"]) [models addObject:url];
    }
    return models;
}

// MARK: Mirdain style

// The colors of Forward's preferences dialog, which match mirdain_client.
static NSColor *hex(uint32_t rgb, CGFloat alpha = 1)
{
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xff) / 255.0 green:((rgb >> 8) & 0xff) / 255.0
        blue:(rgb & 0xff) / 255.0 alpha:alpha];
}
static NSColor *Background() { return hex(0x0a0e11); }
static NSColor *Surface() { return hex(0x111518); }
static NSColor *Border() { return hex(0x3b4e57); }
static NSColor *Text() { return hex(0xe7eef4); }
static NSColor *Accent() { return hex(0xe967c8); }

// Aileron from Forward's resources, without registering it for the whole process.
static NSFont *aileron(bool semiBold, CGFloat size)
{
    static CTFontDescriptorRef descriptors[2] = {};
    auto &descriptor = descriptors[semiBold];
    if (!descriptor) {
        QFile file(semiBold ? ":/assets/fonts/Aileron-SemiBold.otf" : ":/assets/fonts/Aileron-Regular.otf");
        file.open(QIODevice::ReadOnly);
        descriptor = CTFontManagerCreateFontDescriptorFromData((__bridge CFDataRef)file.readAll().toNSData());
    }
    return (__bridge_transfer NSFont *)CTFontCreateWithFontDescriptor(descriptor, size, nullptr);
}

// Draws one line, truncated to rect's width, in a flipped view.
static void drawText(NSString *text, NSFont *font, NSColor *color, NSRect rect)
{
    auto *style = [[NSMutableParagraphStyle alloc] init];
    style.lineBreakMode = NSLineBreakByTruncatingTail;
    [text drawWithRect:rect options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingTruncatesLastVisibleLine
        attributes:@{NSFontAttributeName: font, NSForegroundColorAttributeName: color, NSParagraphStyleAttributeName: style}];
}

// MARK: Views

// One printer's drop target.
@interface PrinterBox : NSView
@property (copy) NSString *name;
@property (copy) void (^dropped)(NSArray<NSURL *> *urls);
@property (nonatomic) BOOL highlighted;
@end

@implementation PrinterBox

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)setHighlighted:(BOOL)highlighted
{
    _highlighted = highlighted;
    self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirtyRect
{
    auto *box = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 0.5, 0.5) xRadius:3 yRadius:3];
    [(_highlighted ? hex(0xe967c8, 0.12) : Surface()) setFill];
    [box fill];
    [(_highlighted ? Accent() : Border()) setStroke];
    [box stroke];
    drawText(_name.length ? _name : @"No printers yet", aileron(true, 13), _highlighted ? Accent() : Text(),
        NSMakeRect(14, (self.bounds.size.height - 18) / 2, self.bounds.size.width - 28, 18));
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    if (!modelURLs(sender.draggingPasteboard).count) return NSDragOperationNone;
    self.highlighted = YES;
    return NSDragOperationCopy;
}

- (void)draggingExited:(id<NSDraggingInfo>)sender { self.highlighted = NO; }

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    self.highlighted = NO;
    NSArray<NSURL *> *urls = modelURLs(sender.draggingPasteboard);
    if (!urls.count) return NO;
    _dropped(urls);
    return YES;
}

@end

// Lays the printer boxes out from the card's top.
@interface CardContent : NSView
@end

@implementation CardContent

- (BOOL)isFlipped { return YES; }

@end

// MARK: Watcher

static constexpr CGFloat CardWidth = 240, Radius = 6, Padding = 10, BoxHeight = 40, Gap = 8;
// Room around the card for its shadow, which the card draws itself: the window's own shadow
// follows the window's rectangle, not the rounded card.
static constexpr CGFloat ShadowMargin = 24;
// How far the card slides while it fades in and out.
static constexpr CGFloat Slide = 8;

struct DragWatcher::Impl {
    QSystemTrayIcon &tray;
    Targets targets;
    Dropped dropped;
    NSPanel *panel = nil;
    id monitor = nil;
    NSTimer *poll = nil;
    bool shown = false;

    NSView *layout(const QList<Target> &printers);
    void show();
    void hide();
};

NSView *DragWatcher::Impl::layout(const QList<Target> &printers)
{
    const auto boxes = printers.isEmpty() ? QList<Target>{Target{}} : printers;
    const CGFloat height = Padding + boxes.size() * BoxHeight + (boxes.size() - 1) * Gap + Padding;
    auto *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, CardWidth + 2 * ShadowMargin, height + 2 * ShadowMargin)];
    auto *card = [[NSView alloc] initWithFrame:NSMakeRect(ShadowMargin, ShadowMargin, CardWidth, height)];
    card.wantsLayer = YES;
    card.layer.backgroundColor = Background().CGColor;
    card.layer.borderColor = Border().CGColor;
    card.layer.borderWidth = 1;
    card.layer.cornerRadius = Radius;
    card.layer.shadowColor = NSColor.blackColor.CGColor;
    card.layer.shadowOpacity = 0.55;
    card.layer.shadowRadius = 14;
    card.layer.shadowOffset = CGSizeMake(0, -6);
    auto shadow = CGPathCreateWithRoundedRect(card.bounds, Radius, Radius, nullptr);
    card.layer.shadowPath = shadow;
    CGPathRelease(shadow);
    [root addSubview:card];

    auto *content = [[CardContent alloc] initWithFrame:card.bounds];
    [card addSubview:content];
    CGFloat y = Padding;
    // By value: the drop block keeps its own copy, since boxes is gone by the time of a drop.
    for (const Target target : boxes) {
        auto *box = [[PrinterBox alloc] initWithFrame:NSMakeRect(Padding, y, CardWidth - 2 * Padding, BoxHeight)];
        box.name = target.name.toNSString();
        box.dropped = ^(NSArray<NSURL *> *urls) {
            QStringList paths;
            for (NSURL *url in urls) paths.append(QString::fromNSString(url.path));
            hide();
            dropped(paths, target);
        };
        [content addSubview:box];
        y += BoxHeight + Gap;
    }
    return root;
}

void DragWatcher::Impl::show()
{
    auto *content = layout(targets());
    const auto size = content.frame.size;
    // Qt reports the icon in top-left-origin coordinates of the primary screen; AppKit's are bottom-left.
    const auto icon = tray.geometry();
    const auto primary = NSScreen.screens.firstObject.frame;
    NSPoint top;
    if (icon.isValid()) {
        top = NSMakePoint(icon.center().x(), NSMaxY(primary) - icon.bottom() - 6);
    } else {
        // The icon can be hidden behind the notch or a crowded menu bar.
        const auto screen = NSScreen.mainScreen.visibleFrame;
        top = NSMakePoint(NSMaxX(screen) - CardWidth / 2 - 12, NSMaxY(screen) - 6);
    }
    const auto frame = NSMakeRect(top.x - size.width / 2, top.y + ShadowMargin - size.height, size.width, size.height);
    panel.contentView = content;
    [panel setFrame:NSOffsetRect(frame, 0, Slide) display:YES];
    panel.alphaValue = 0;
    [panel orderFrontRegardless];
    shown = true;
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.18;
        context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
        panel.animator.alphaValue = 1;
        [panel.animator setFrame:frame display:YES];
    }];
}

void DragWatcher::Impl::hide()
{
    if (!shown) return;
    shown = false;
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.14;
        context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseIn];
        panel.animator.alphaValue = 0;
        [panel.animator setFrame:NSOffsetRect(panel.frame, 0, Slide) display:YES];
    } completionHandler:^{
        // A drag that started during the fade shows the panel again.
        if (!shown) [panel orderOut:nil];
    }];
}

DragWatcher::DragWatcher(QSystemTrayIcon &tray, Targets targets, Dropped dropped)
    : impl(new Impl{tray, std::move(targets), std::move(dropped)})
{
    auto *panel = [[NSPanel alloc] initWithContentRect:NSZeroRect
        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
        backing:NSBackingStoreBuffered defer:YES];
    panel.level = NSStatusWindowLevel;
    panel.opaque = NO;
    panel.backgroundColor = NSColor.clearColor;
    panel.hasShadow = NO;
    panel.hidesOnDeactivate = NO;
    panel.releasedWhenClosed = NO;
    panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
    impl->panel = panel;

    // A drag in another app writes the system drag pasteboard once it starts. Each mouse down
    // polls it until the button is released, so nothing runs while the mouse is idle; comparing
    // its change count with the one at mouse down ignores what a previous drag left there.
    // Drags out of a Dock stack don't write this pasteboard, so they show no panel.
    Impl *state = impl.get();
    NSPasteboard *pasteboard = [NSPasteboard pasteboardWithName:NSPasteboardNameDrag];
    impl->monitor = [NSEvent addGlobalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown handler:^(NSEvent *) {
        [state->poll invalidate];
        const auto start = pasteboard.changeCount;
        __block bool checked = false;
        state->poll = [NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *timer) {
            if (!(NSEvent.pressedMouseButtons & 1)) {
                [timer invalidate];
                // Let a drop on the panel finish before it closes.
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                    if (!(NSEvent.pressedMouseButtons & 1)) state->hide();
                });
                return;
            }
            if (checked || pasteboard.changeCount == start) return;
            checked = true;
            if (modelURLs(pasteboard).count) state->show();
        }];
    }];
}

DragWatcher::~DragWatcher()
{
    [NSEvent removeMonitor:impl->monitor];
    [impl->poll invalidate];
    [impl->panel orderOut:nil];
}
