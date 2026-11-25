#include "platform/macos/MirdainStyle.h"

#include <QFile>
#include <QSystemTrayIcon>

#import <CoreText/CoreText.h>
#import <QuartzCore/QuartzCore.h>

static constexpr CGFloat CardRadius = 6, NotchWidth = 16, NotchHeight = 7;

NSColor *mirdainColor(uint32_t rgb, CGFloat alpha)
{
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xff) / 255.0 green:((rgb >> 8) & 0xff) / 255.0
        blue:(rgb & 0xff) / 255.0 alpha:alpha];
}

NSFont *aileron(bool semiBold, CGFloat size)
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

// Lays content out from the card's top.
@interface MirdainCardContent : NSView
@end

@implementation MirdainCardContent
- (BOOL)isFlipped { return YES; }
@end

// The card's outline in layer coordinates (y up), with the notch rising from its top edge.
// Inset half a point so the one-point border stays crisp.
static CGPathRef cardPath(CGFloat width, CGFloat height, bool notch)
{
    const CGFloat left = 0.5, right = width - 0.5, bottom = 0.5, top = height - 0.5, middle = width / 2;
    auto path = CGPathCreateMutable();
    CGPathMoveToPoint(path, nullptr, left + CardRadius, bottom);
    CGPathAddArcToPoint(path, nullptr, right, bottom, right, top, CardRadius);
    CGPathAddArcToPoint(path, nullptr, right, top, left, top, CardRadius);
    if (notch) {
        CGPathAddLineToPoint(path, nullptr, middle + NotchWidth / 2, top);
        CGPathAddLineToPoint(path, nullptr, middle, top + NotchHeight);
        CGPathAddLineToPoint(path, nullptr, middle - NotchWidth / 2, top);
    }
    CGPathAddArcToPoint(path, nullptr, left, top, left, bottom, CardRadius);
    CGPathAddArcToPoint(path, nullptr, left, bottom, right, bottom, CardRadius);
    CGPathCloseSubpath(path);
    return path;
}

NSView *mirdainCard(NSSize size, NSView **content, bool notch)
{
    // The notch sits above the content, so the card is that much taller.
    const CGFloat extra = notch ? NotchHeight : 0;
    auto *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, size.width + 2 * MirdainCardShadowMargin,
        size.height + extra + 2 * MirdainCardShadowMargin)];
    auto *card = [[NSView alloc] initWithFrame:NSMakeRect(MirdainCardShadowMargin, MirdainCardShadowMargin, size.width, size.height + extra)];
    card.wantsLayer = YES;
    [card.layer setValue:@(notch) forKey:@"mirdainNotch"];
    auto path = cardPath(size.width, size.height, notch);
    auto *shape = [CAShapeLayer layer];
    shape.path = path;
    shape.fillColor = mirdainBackground().CGColor;
    shape.strokeColor = mirdainBorder().CGColor;
    shape.lineWidth = 1;
    [card.layer addSublayer:shape];
    card.layer.shadowColor = NSColor.blackColor.CGColor;
    card.layer.shadowOpacity = 0.55;
    card.layer.shadowRadius = 14;
    card.layer.shadowOffset = CGSizeMake(0, -6);
    card.layer.shadowPath = path;
    CGPathRelease(path);
    [root addSubview:card];
    *content = [[MirdainCardContent alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
    [card addSubview:*content];
    return root;
}

// Borderless windows can't become key unless they say so.
@interface MirdainKeyPanel : NSPanel
@end

@implementation MirdainKeyPanel
- (BOOL)canBecomeKeyWindow { return YES; }
// AppKit moves key windows clear of the menu bar, which the panel's shadow margin overlaps.
- (NSRect)constrainFrameRect:(NSRect)frame toScreen:(NSScreen *)screen { return frame; }
@end

NSView *mirdainCardContent(NSView *root)
{
    return root.subviews.firstObject.subviews.firstObject;
}

void mirdainGrowCard(NSView *root, NSSize from, CFTimeInterval duration)
{
    NSView *card = root.subviews.firstObject;
    auto *shape = (CAShapeLayer *)card.layer.sublayers.firstObject;
    const bool notch = [[card.layer valueForKey:@"mirdainNotch"] boolValue];
    const auto to = mirdainCardContent(root).frame.size;
    // The old outline, centered and top-aligned in the new card (layer coordinates run upward).
    auto offset = CGAffineTransformMakeTranslation((to.width - from.width) / 2, to.height - from.height);
    auto start = cardPath(from.width, from.height, notch);
    auto moved = CGPathCreateCopyByTransformingPath(start, &offset);
    CGPathRelease(start);
    for (NSString *key in @[@"path", @"shadowPath"]) {
        auto *grow = [CABasicAnimation animationWithKeyPath:key];
        grow.fromValue = (__bridge id)moved;
        grow.duration = duration;
        grow.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
        [[key isEqualToString:@"path"] ? shape : card.layer addAnimation:grow forKey:key];
    }
    CGPathRelease(moved);
}

NSPanel *mirdainPanel(bool keyable)
{
    NSPanel *panel = [[(keyable ? MirdainKeyPanel.class : NSPanel.class) alloc] initWithContentRect:NSZeroRect
        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
        backing:NSBackingStoreBuffered defer:YES];
    panel.level = NSStatusWindowLevel;
    panel.opaque = NO;
    panel.backgroundColor = NSColor.clearColor;
    panel.hasShadow = NO;
    panel.hidesOnDeactivate = NO;
    panel.releasedWhenClosed = NO;
    panel.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
    return panel;
}

NSRect mirdainTrayPanelFrame(QSystemTrayIcon &tray, NSSize size)
{
    // Qt reports the icon in top-left-origin coordinates of the primary screen; AppKit's are bottom-left.
    const auto icon = tray.geometry();
    const auto primary = NSScreen.screens.firstObject.frame;
    NSPoint top;
    if (icon.isValid()) {
        top = NSMakePoint(icon.center().x(), NSMaxY(primary) - icon.bottom() - 6);
    } else {
        // The icon can be hidden behind the notch or a crowded menu bar.
        const auto screen = NSScreen.mainScreen.visibleFrame;
        top = NSMakePoint(NSMaxX(screen) - (size.width - 2 * MirdainCardShadowMargin) / 2 - 12, NSMaxY(screen) - 6);
    }
    return NSMakeRect(top.x - size.width / 2, top.y + MirdainCardShadowMargin - size.height, size.width, size.height);
}
