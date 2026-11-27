#!/bin/sh
# Re-signs a deployed Forward.app with a Developer ID identity, hardened runtime and a
# secure timestamp, inside out, as notarization requires.
# Usage: sign.sh "<app bundle>" "Developer ID Application: Mirdain Systems Inc. (NYDPRB5WDB)"
set -e
app=$1
identity=$2
sign() { /usr/bin/codesign --force --timestamp --options runtime --sign "$identity" "$@"; }

# Sparkle's documented order; the Downloader service keeps its own entitlements.
sparkle="$app/Contents/Frameworks/Sparkle.framework"
sign "$sparkle/Versions/B/XPCServices/Installer.xpc"
sign --preserve-metadata=entitlements "$sparkle/Versions/B/XPCServices/Downloader.xpc"
sign "$sparkle/Versions/B/Autoupdate" "$sparkle/Versions/B/Updater.app"
sign "$sparkle"

find "$app/Contents/Frameworks" "$app/Contents/PlugIns" -type f -name '*.dylib' -exec sh -c '
    identity=$1; shift
    /usr/bin/codesign --force --timestamp --options runtime --sign "$identity" "$@"
' sh "$identity" {} +
find "$app/Contents/Frameworks" -maxdepth 1 -name '*.framework' ! -name Sparkle.framework -exec sh -c '
    identity=$1; shift
    /usr/bin/codesign --force --timestamp --options runtime --sign "$identity" "$@"
' sh "$identity" {} +
sign "$app"
/usr/bin/codesign --verify --deep --strict "$app"
