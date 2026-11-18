#include "platform/SupportedBrowser.h"

#include <QDesktopServices>
#include <QUrl>

#import <AppKit/AppKit.h>

void openInSupportedBrowser(const QUrl &url)
{
    NSWorkspace *workspace = NSWorkspace.sharedWorkspace;
    NSURL *target = url.toNSURL();
    NSString *defaultBrowser = [NSBundle bundleWithURL:[workspace URLForApplicationToOpenURL:target]].bundleIdentifier;
    if ([defaultBrowser hasPrefix:@"com.apple.Safari"]) {
        for (NSString *identifier in @[@"com.google.Chrome", @"com.microsoft.edgemac", @"com.brave.Browser",
                 @"company.thebrowser.Browser", @"org.mozilla.firefox", @"org.chromium.Chromium"]) {
            NSURL *browser = [workspace URLForApplicationWithBundleIdentifier:identifier];
            if (!browser) continue;
            [workspace openURLs:@[target] withApplicationAtURL:browser
                  configuration:[NSWorkspaceOpenConfiguration configuration] completionHandler:nil];
            return;
        }
    }
    QDesktopServices::openUrl(url);
}
