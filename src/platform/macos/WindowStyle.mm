#include "platform/macos/WindowStyle.h"

#include <QWidget>

#import <AppKit/AppKit.h>

void applyDarkTitleBar(QWidget &widget)
{
    // winId() creates the native window before the dialog is shown.
    auto *view = (__bridge NSView *)reinterpret_cast<void *>(widget.winId());
    auto *window = view.window;
    window.animationBehavior = NSWindowAnimationBehaviorNone;
    const auto background = widget.palette().color(QPalette::Window);
    window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    window.backgroundColor = [NSColor colorWithSRGBRed:background.redF()
        green:background.greenF() blue:background.blueF() alpha:1.0];
    window.titlebarAppearsTransparent = YES;
}
