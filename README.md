# Mirdain Forward

Licensed under the [GNU General Public License v3.0](LICENSE) (GPL-3.0-only).
Third-party components retain their respective licenses.

Minimal Qt C++ menu bar / system tray app. It listens on **127.0.0.1:18744**,
downloads a `.mirdain` archive, and sends the selected slice's **plate 0** to an
ELEGOO, PrusaLink, or Bambu LAN printer, then requests printing. No account or
queue. Printer names, IDs, and settings are saved locally; printer credentials
arrive in each request.

The menu lists each printer directly, followed by Check for Updates, Preferences, the current transfer status, and Quit. Only one
transfer runs at a time. Success means the printer accepted the print request, not that the
first layer has begun. Busy printers are not interrupted. Start commands are
never automatically retried; after an unconfirmed start, check the printer
before sending again.

ELEGOO and PrusaLink receive the plain `.gcode` inside the native plate package;
Bambu receives the original `.gcode.3mf`. Temporary downloads are deleted after
each transfer. Archive members are read without extracting a directory tree.
Long menu status messages are ellipsized; the tray tooltip retains the full text.

## Startup and notification preferences

On the first successful launch, Forward asks whether to **launch when you sign
in** and **notify you when a plate is sent through Forward**. Both choices default
to off. Save your choices, or choose **Skip** to keep the defaults; either option
dismisses setup for future launches. Closing the setup dialog also skips it.
Choose **Preferences…** from the menu bar / system tray to change either choice
later. Existing users see the setup dialog once after updating.

Automatic startup is registered only when you explicitly enable it. On macOS
13+, `src/platform/macos/StartupRegistration.mm` uses ServiceManagement's
[`SMAppService.mainAppService`](https://developer.apple.com/documentation/servicemanagement/smappservice/mainapp?language=objc)
to register the existing menu bar app, without a separate helper. If macOS needs
approval, Forward directs you to Login Items in System Settings. Earlier macOS
versions and Linux show the startup option as unavailable.

On Windows, `src/platform/windows/StartupRegistration.cpp` uses Qt's `QSettings` registry support
to store the quoted executable path as **Mirdain Forward** in
[`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`](https://learn.microsoft.com/en-us/windows/win32/setupapi/run-and-runonce-registry-keys).
It affects only the current user and requires no administrator access. Disabling
the option removes that value. Windows may delay startup after sign-in.

Move the macOS app or extract the complete Windows app folder to its permanent
location before enabling startup. After moving a registered Windows app, disable
and re-enable startup from the new location to update the path. Preferences also
offers **Open startup settings…**. The Windows checkbox indicates the Run entry
is registered; Windows Settings or Task Manager can disable it separately.
Forward never re-registers startup merely because it launches or its saved
preference is enabled, so OS-level changes are respected. Reopening Forward
while it is already running exits quietly.

`AppPreferences` owns the shared setup/preferences dialog. `StartupRegistration`
and `NotificationSender` provide platform-specific behavior behind shared class
interfaces. `AppConfig` reads and atomically writes sections of the existing
`printers.json`, preserving both printer settings and app preferences:

```json
{
  "printers": {},
  "preferences": {
    "setup_complete": true,
    "launch_at_sign_in": false,
    "notifications_enabled": false
  }
}
```

The saved startup choice records the user's last selection; the preferences UI
reads current OS registration instead of automatically applying that choice.
Older files containing only `printers` remain readable.

## App updates

Forward integrates Sparkle 2.10.1 on macOS and WinSparkle 0.9.4 on Windows.
CMake downloads the pinned SDKs with SHA-256 verification and bundles the
framework/DLL. **Check for Updates…** uses the native updater UI. Preferences
and first-launch setup offer **Automatically check for updates**, initially off.
Update preferences are stored by each native updater (NSUserDefaults on macOS,
WinSparkle's registry settings on Windows), so changes in native update dialogs
are reflected in Preferences. Forward does not duplicate them in `printers.json`.
The updater starts after setup finishes. Users choose when to install; Forward
does not enable unattended installation.

Configure both public release values at CMake configure time:

```sh
cmake -S . -B build \
  -DFORWARD_UPDATE_FEED_URL="https://downloads.example.com/forward/macos-arm64.xml" \
  -DFORWARD_UPDATE_PUBLIC_KEY="YOUR_BASE64_ED25519_PUBLIC_KEY"
```

Use a separate feed for each macOS architecture and for Windows x64. Feed URLs
must use HTTPS; a public verification key is required. With neither value set,
development builds still bundle the updater but disable update controls and do
not contact an update server. Supplying only one value fails configuration.
Linux builds show unavailable update controls.

macOS delays an updater-requested relaunch until the current transfer completes
and refuses new transfers while that installation is pending. On Windows, an
installation attempt during a transfer is declined with a message asking the user
to finish the transfer and install again. Once shutdown is reserved, new requests
are rejected before any download or print command. Checks and update downloads
can run during transfers. This guard concerns sending a plate; it does not wait
for a printer to finish printing after accepting a print request.

Windows updates require installation using the new per-user Inno Setup installer.
Portable ZIP copies do not enable updating, since an installer would otherwise
create a different installation instead of replacing the running copy. The
installer records its directory in HKCU and preserves the user's app data.
Its running-app mutex prevents replacement while Forward is running, including
when the installer is launched manually. Updates reuse the existing installation
directory and launch Forward afterward. Existing portable users should quit
Forward, run the installer once, and, if startup was enabled, disable it in the
old copy and enable it in the installed copy.

Existing releases without an updater require one manual installation of an
updater-enabled release. Publishing feeds and signed packages is separate from
building the application. See [release instructions](packaging/updates/README.md)
for CI variables, feed templates, signing, and publishing order.

## Notifications

When notifications are enabled in Preferences, Forward sends one desktop
notification when it accepts a **Send and print**
request, before downloading the archive: **Print request received**, followed by
the printer name. This confirms receipt, not that printing has started. Rejected
requests do not notify, and progress, success, and failure do not send additional
notifications.

On macOS, saving Preferences with notifications enabled asks for notification
permission. Receiving a plate never requests permission. If permission was
denied, saving with notifications enabled explains how to enable it in **System
Settings > Notifications > Mirdain Forward**. Forward requests banners without sound.
Windows and Linux use Qt's system tray notifications and the desktop's existing
notification settings. System settings or Focus mode may suppress notifications.
Turning off notifications in Preferences suppresses delivery on every platform.

`NotificationSender` hides permission handling and platform delivery from the
shared request handler. macOS uses UserNotifications through Objective-C++;
other platforms use `QSystemTrayIcon::showMessage()`.

## Printer settings

Printers are remembered when a valid **Send and print** request arrives. Open
**printer name > Auto-level bed** in the menu bar / system tray to
change its setting. It defaults to enabled and applies to subsequent requests.
Settings unsupported by a printer's sender are hidden. The printer submenus are
disabled during a transfer.

The custom printer ID is the storage key, so renaming a printer or changing its
connection preserves its settings. Requests without a custom printer ID use the
protocol, hostname, and port as their key and display name instead.

Settings and app preferences are loaded at startup and saved atomically to `printers.json` under
Qt's `QStandardPaths::AppDataLocation`:

| Platform | Default file |
| --- | --- |
| macOS | `~/Library/Application Support/Mirdain Forward/printers.json` |
| Windows | `%APPDATA%/Mirdain Forward/printers.json` |
| Linux | `~/.local/share/Mirdain Forward/printers.json` (honors `XDG_DATA_HOME`) |

For example:

```json
{
  "printers": {
    "custom:printer-uuid": {
      "custom_printer_id": "printer-uuid",
      "custom_printer_name": "Workshop Bambu",
      "name": "Workshop Bambu",
      "protocol": "bambu_lan",
      "settings": {
        "auto_level_bed": true
      }
    }
  }
}
```

Only printer identity, protocol, printer settings, and app preferences are persisted. Download URLs,
authorization, and printer credentials are not saved.
For ELEGOO the setting controls SDCP `Calibration_switch` (1 or 0); for Bambu it
controls `bed_leveling` and `bed_levelling`. PrusaLink's upload-and-print API has
no leveling option, so its toggle is disabled and leveling follows the sliced
G-code's start sequence.

## Source layout

```text
src/
  main.cpp              HTTP forwarding and app lifecycle
  app/                  Shared JSON config and preferences UI
  archive/              ZIP archive reader
  platform/             Startup, notification, and updater interfaces
    macos/              Native macOS implementations
    windows/            Windows startup registration and Qt notifications
    generic/            Fallback notifications and unsupported startup
  printers/             Printer settings, senders, and send jobs
packaging/
  macos/                App bundle Info.plist template
  windows/              Executable resource template
assets/                 App icons and tray artwork
```

CMake selects the platform implementations. Project headers use paths relative
to `src/`, such as `platform/NotificationSender.h` and `printers/PrinterSender.h`.

## Adding printer protocols

`PrinterSettings` retains one `PrinterSender` in memory for each registered
printer, including printers restored at startup. `PrinterSender::create(protocol)`
constructs a sender without connection details or network activity. Its virtual
`loadJson()` and `toJson()` methods restore and serialize printer identity and
settings using the JSON format above.

The menu uses `supportsAutoLevelBed()` and `autoLevelBedEnabled()` to read each
sender's capabilities and current value, and calls `setAutoLevelBedEnabled()`
when the user changes it. Capability support belongs to the concrete sender;
the menu contains no protocol-specific checks. ELEGOO and Bambu support this
setting, while PrusaLink inherits the default unsupported capability.

`fileFormat()` selects plain G-code or the native plate archive.
`send(PrinterFile, connection)` creates a temporary `PrinterSendJob` with the
sender's current setting. Each job owns its connection validation, sockets,
timers, upload state, and print request, and is discarded on completion or
failure. The retained sender survives and can send again with a fresh job.
Constructing or restoring a sender never contacts a printer; network activity
starts only when sending a file. Progress and completion signals are forwarded
through the sender to the shared HTTP/tray response flow.

To add another protocol, add its sources under `src/printers/`, subclass
`PrinterSender`, declare its protocol, default
port, file format, and supported settings, and implement `createJob()` using a
`PrinterSendJob` subclass. Add its sources to CMake and a branch to
`PrinterSender::create(protocol)`. The HTTP handler, JSON persistence, archive
download, plate selection, and settings menu remain shared.

## Build and run

Requires Qt 6.8+ with Widgets, Network, HTTP Server, WebSockets, MQTT, and Concurrent, CMake,
a C/C++17 compiler, and OpenSSL 3 development libraries. CMake downloads and statically
links pinned miniz 3.1.2, Prusa libbgcode, Heatshrink 0.4.1, and zlib 1.3.1 sources
on first configure; no external unzip or G-code conversion tool is needed.
Dependency examples, fuzzers, and tests are disabled.
Bambu uploads use Qt's OpenSSL TLS backend directly, with TLS session reuse
between the control and data connections. macOS and Windows builds bundle the
OpenSSL runtime. Credentials stay in memory rather than process arguments.

### macOS

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
open "build/Mirdain Forward.app"

# Build a distributable disk image (also builds the app if needed).
cmake --build build --target package
```

On macOS, every link automatically bundles Qt's frameworks and plugins and
ad-hoc signs the app. No separate `macdeployqt` command is needed. Stale deployed
files are removed before linking, so rebuilding cannot mix bundled and Homebrew
Qt libraries. `LSUIElement` keeps the app in the menu bar without a Dock icon.
Allow local network access if macOS requests it.

Packaging uses CPack's DragNDrop generator to create
`dist/mirdain_forward-macos-arm64.dmg` on Apple Silicon or
`dist/mirdain_forward-macos-x86_64.dmg` on Intel. Explicit multi-architecture
builds use the `universal` suffix. The DMG contains the deployed app, an
**Applications** shortcut, and a **Licenses** folder. Open it, drag
`Mirdain Forward.app` to **Applications**, eject the image, and launch the app
from Applications before enabling automatic startup. Packaging verifies the
app's signature and preserves its bundled frameworks, plugins, and permissions.
These builds use ad-hoc signing and are not notarized with Apple. See
[update release instructions](packaging/updates/README.md) before publishing updates.

### Windows

Install Visual Studio 2022 C++ build tools, OpenSSL 3 for x64, and Qt's MSVC 2022 x64 kit, including
the HTTP Server, WebSockets, and Image Formats modules. Build and install the
matching Qt MQTT module from source as shown in the workflow below; it is not
included in the public SDK binaries. From a Visual Studio developer PowerShell
prompt (adjust the Qt and OpenSSL paths for your installation):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.9.2/msvc2022_64 -DOPENSSL_ROOT_DIR="C:/Program Files/OpenSSL-Win64"
cmake --build build --config Release --parallel
.\build\Release\mirdain_forward.exe
```

Each link runs `windeployqt` to copy Qt DLLs, OpenSSL DLLs, plugins, and compiler runtime files
beside the executable. For a portable copy, distribute the whole `build/Release` folder.
For an installed copy with updater support, first add the README and licenses
as in CI, then compile `build/installer.iss` with Inno Setup 6. This template
expects the multi-configuration `build/Release` output above. The installer
is written to `dist/mirdain_forward-windows-x64-setup.exe`. The executable
uses the supplied app icon and runs in the system tray without a console window.
Printer credentials and the localhost API work the same way as on macOS.

On Linux, use your Qt installation's CMake prefix and run `build/mirdain_forward`
in a desktop environment with a system tray and the OpenSSL 3 runtime installed.

## GitHub Actions builds

The **Build apps** workflow in `.github/workflows/build.yml` runs on pushes to
`main`, pull requests, and manual **Actions > Build apps > Run workflow** runs.
It uses pinned action revisions and Qt 6.9.2, caches the SDK, and builds:

| Artifact | Runner | Contents |
| --- | --- | --- |
| `mirdain_forward-macos-arm64` | `macos-15` | Apple Silicon DMG with bundled Qt app and Applications shortcut |
| `mirdain_forward-macos-x86_64` | `macos-15-intel` | Intel DMG with bundled Qt app and Applications shortcut |
| `mirdain_forward-windows-x64` | `windows-2022` | Per-user setup installer and portable ZIP with Qt DLLs, plugins, and compiler runtime |

Qt MQTT is built from the pinned Qt 6.9.2 source revision and installed into the
CI SDK because the public SDK catalog does not provide MQTT binaries. It uses
Ninja with a single Release configuration; build and install are separate steps
so installation only runs after a successful build. Its tests and examples are
disabled. Windows ZIPs also include the MSVC runtime DLLs
beside the executable so they can run without a separate runtime installation.

Once the workflow file is pushed, open a completed run's **Artifacts** section
to download your platform's artifact. Artifacts are kept for 14 days. Unzip the
macOS artifact to find the DMG; Windows artifacts contain both the setup installer and the portable app ZIP. macOS DMGs
preserve the app bundle's symlinks and permissions. macOS DMGs collect licenses
in a **Licenses** folder and omit the README. Windows ZIPs include the README
and licenses. Third-party licenses include libbgcode (AGPL-3.0), Heatshrink, and zlib.
macOS uses ad-hoc signing; Windows builds are unsigned. This
workflow builds and packages only; it does not run tests or publish releases.
The separate **Release Forward** workflow handles version tags, update signing,
GitHub Releases, and Aperture feed synchronization after one-time configuration.
See [release setup](packaging/updates/README.md).

## Request

`POST http://127.0.0.1:18744/send` with `Content-Type: application/json`:

```json
{
  "url": "https://example.com/download/part.mirdain",
  "custom_printer_id": "printer-uuid",
  "custom_printer_name": "Workshop printer",
  "connection": {
    "protocol": "elegoo_sdcp",
    "host": "10.0.0.220",
    "port": 3030
  }
}
```

Use a publicly downloadable or signed URL, or provide
`"download_headers": {"Authorization": "Bearer …"}` for an authenticated archive.
Only this Authorization header is forwarded to the download URL; it is not sent
to the printer or saved. Authenticated downloads reject cross-origin redirects.
The web slicer supplies its session-scoped slicing grant and archive endpoint.
Omit `slice_id` to send the first slice in the manifest,
or include `"slice_id": "the-slice-id"` to select one. In either case, it sends
plate coordinate 0 (Plate 1 in the slicer).

Success is HTTP 200 with
`{"ok":true,"print_requested":true,"filename":"…gcode","plate":0}`.
Invalid requests return 400, overlapping transfers return 409, and failed
downloads, invalid archives, rejected uploads, or failed starts return 502 with
`{"error":"…"}`. A timeout after sending start may mean the command was accepted;
check the printer before retrying. The app does not automatically retry start.

## Connection protocols

### ELEGOO (`elegoo_sdcp`)

```json
{"protocol":"elegoo_sdcp","host":"10.0.0.220","port":3030}
```

The port defaults to 3030. Uploads use ELEGOO's 1 MiB multipart protocol with a
whole-file MD5 checksum. The app discovers the MainboardID over UDP port 3000,
checks the printer is idle over WebSocket, and sends SDCP Cmd 128 with the
uploaded `/local/` path. Automatic bed leveling follows the saved printer setting;
time-lapse is disabled, and platform type comes from the slice's bed type
(textured versus smooth/cool).
Success requires the start-command acknowledgment.

### PrusaLink (`prusalink`)

```json
{"protocol":"prusalink","host":"10.0.0.221","api_key":"YOUR_API_KEY"}
```

Alternatively, use HTTP Digest credentials:

```json
{"protocol":"prusalink","host":"10.0.0.221","username":"maker","password":"YOUR_PASSWORD"}
```

The port defaults to 80. `username` defaults to `maker`; an API key takes
precedence when both authentication methods are configured. `storage` optionally
selects `usb`, `local`, or `sdcard`. The app checks printer readiness first.
Modern PrusaLink uses the first available writable storage unless configured,
then PUTs the G-code with `Print-After-Upload: ?1` and `Overwrite: ?0`.
Upload filenames use at most 32 characters from the original stem plus a fresh
UUID and the file extension, keeping the URL within the firmware's 168-byte limit.
When `/api/version` reports printer firmware 5.1.0 or newer and PUT support,
Forward first converts the extracted G-code to `.bgcode` with
[Prusa's libbgcode](https://github.com/prusa3d/libbgcode). Conversion runs in the
background using MeatPack with comments, Heatshrink 12/4 compression, and CRC32
checksums. Commands and comments are retained, and thumbnails and print/printer
metadata are included in the binary blocks. The menu shows compression followed
by upload progress for the compressed size. The saved `.mirdain` archive is unchanged.
Older printers and firmware that does not report its version receive plain G-code.
Legacy PrusaLink uses multipart upload with `select=true` and `print=true`,
defaulting to `local` storage. Success requires a successful HTTP response to
upload-and-print. Upload progress shows percentage, transferred/total MiB, and
approximate KiB/s at most once per second, then indicates that the app is waiting
for the upload-and-print response. Legacy multipart totals include the form data.
Leveling follows the sliced G-code's start sequence.

These are native requests, so the printer does not need browser CORS support.
See the [PrusaLink API specification](https://github.com/prusa3d/Prusa-Link-Web/blob/master/spec/openapi.yaml).

### Bambu LAN (`bambu_lan`)

```json
{
  "protocol":"bambu_lan",
  "host":"10.0.0.222",
  "serial_number":"YOUR_PRINTER_SERIAL",
  "access_code":"YOUR_LAN_ACCESS_CODE"
}
```

Enable LAN Only mode and, on firmware with authorization controls, Developer Mode.
This sender uses direct LAN access; it does not use Bambu Cloud or Bambu Connect.
See [Bambu's Developer Mode explanation](https://blog.bambulab.com/updates-and-third-party-integration-with-bambu-connect/).
`port` defaults to MQTT TLS port 8883; `ftp_port` defaults to implicit FTPS port
990. Both services authenticate as `bblp` with the LAN access code. The app accepts
the printer's self-signed TLS certificate for these LAN connections.

The app checks the MQTT status, uploads the native `.gcode.3mf` over FTPS,
checks the printer is still idle, then publishes `project_file` with the archive's
actual `Metadata/plate_N.gcode` path and G-code MD5. The archive's
`Metadata/project_settings.config` identifies the printer model: H2D/X2D use the
storage root and `ftp:///` print URLs; other models use `/cache` and
`file:///sdcard/cache/` URLs, matching `mirdain_printer_client`. Missing model
metadata defaults to `/cache`.

Before sending any `.gcode.3mf`, Forward rebuilds the ZIP without directories
named `Objects` (including `3D/Objects/`). All retained entries are copied in
compressed form, preserving their contents and compression without recompressing
the G-code. This also reduces uploads from older saved projects.

FTPS reuses the control TLS session for the passive data connection. As in the
printer client, servers advertising `vsFTPd` receive TLS `close_notify`; other
servers, including P1P, get a direct data-socket close to avoid hanging on TLS
shutdown. Uploads keep a bounded 256 KiB queue fed with 8 KiB writes and show
percentage, transferred/total MiB, and approximate KiB/s once per second.
Uploads fail after 15 seconds without data progress or a command reply;
there is no overall time limit while the upload continues making progress. Success still
requires the server's final FTP completion reply. P1P/P1S allow 180 seconds for
the subsequent print acknowledgment; other models allow 30 seconds.
Success requires the matching MQTT command acknowledgment, not just successful
upload or MQTT delivery. The start request uses the saved bed-leveling setting
and disables time-lapse, flow calibration, vibration calibration, and layer inspection.

By default it uses the external spool (`use_ams=false`, `ams_mapping=[254]`).
For AMS, add `"use_ams":true,"ams_mapping":[0]`: provide one global slot ID per
sliced filament (`AMS index * 4 + slot index`, both zero-based; `-1` for unused).
Forward passes this array directly from the request's `connection` into the
MQTT start command. It does not derive AMS slots from the `.mirdain` manifest,
read `Metadata/mirdain_filament_bindings.json` for mapping, or check that the
array length matches the selected plate's filament count. The array must be in
the native plate's slicer filament order, not the outer project's filament order.
The preset must be sliced for the target Bambu printer. This implements the
standard LAN `project_file` flow; firmware requiring signed commands remains
unsupported without Developer Mode. No physical Prusa or Bambu printer was used
to verify this implementation.

The server handles CORS preflights for `https://slicer.mirdainsystems.com`,
`http://localhost:5173`, and `http://127.0.0.1:5173`. Other browser origins are
rejected. Requests without an Origin header, such as native clients, are allowed.
The server binds only to IPv4 loopback. The browser may request **loopback/local
network access**; CORS between the website and this app is handled here, and
the app's native requests to the printer are not subject to browser CORS.

The web slicer's **Send and print** button uses this endpoint directly. There is no
separate health check or availability polling; an unreachable app causes the send
request to fail. Browser integration:

```js
const response = await fetch('http://127.0.0.1:18744/send', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  targetAddressSpace: 'loopback',
  body: JSON.stringify({
    url: downloadUrl,
    download_headers: { Authorization: `Bearer ${slicingToken}` },
    connection: mirdain_connection,
    slice_id: sliceId,
    custom_printer_id: customPrinterId,
    custom_printer_name: customPrinterName,
  }),
})
const result = await response.json()
if (!response.ok) throw new Error(result.error)
```
