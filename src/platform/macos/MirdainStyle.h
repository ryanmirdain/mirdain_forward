#pragma once

#import <AppKit/AppKit.h>

class QSystemTrayIcon;

// The look Forward's panels under the menu bar icon share: Mirdain's colors (as in Forward's
// pages and mirdain_client), Aileron, and a rounded card that casts its own shadow.

NSColor *mirdainColor(uint32_t rgb, CGFloat alpha = 1);
inline NSColor *mirdainBackground() { return mirdainColor(0x0a0e11); }
inline NSColor *mirdainSurface() { return mirdainColor(0x111518); }
inline NSColor *mirdainLine() { return mirdainColor(0x29383f); }
inline NSColor *mirdainBorder() { return mirdainColor(0x3b4e57); }
inline NSColor *mirdainText() { return mirdainColor(0xe7eef4); }
inline NSColor *mirdainMuted() { return mirdainColor(0x9dacbc); }
inline NSColor *mirdainAccent() { return mirdainColor(0xe967c8); }

// Aileron from Forward's resources, without registering it for the whole process.
NSFont *aileron(bool semiBold, CGFloat size);

// Room around a card for its shadow, which the card draws itself: a window's own shadow
// follows the window's rectangle, not the rounded card.
constexpr CGFloat MirdainCardShadowMargin = 24;

// A transparent root view holding a card of size, inset by the shadow margin. Content goes in
// the returned card's flipped content view (the card's only subview). Size the panel to the
// root before making it the panel's content, so the card is never squeezed to the old size. With notch, a small arrow
// on the card's top center points up at the menu bar icon the card hangs from.
NSView *mirdainCard(NSSize size, NSView **content, bool notch = false);

// The card's content view, and its size (without the notch), in a root from mirdainCard.
NSView *mirdainCardContent(NSView *root);
// Animates a card from mirdainCard growing from a card of from size to its own, keeping its top
// edge in place: its outline and shadow widen and lengthen downward. Fade its content in alongside.
void mirdainGrowCard(NSView *root, NSSize from, CFTimeInterval duration);

// A borderless panel at the status-bar level that never activates Forward and shows on every
// Space, for content built with mirdainCard. With keyable, it can take keyboard input, such as
// for a text field, still without activating Forward.
NSPanel *mirdainPanel(bool keyable = false);

// The frame for a panel of size whose card hangs centered under the tray icon, in AppKit
// screen coordinates; or near the menu bar's right end when the icon is hidden.
NSRect mirdainTrayPanelFrame(QSystemTrayIcon &tray, NSSize size);
