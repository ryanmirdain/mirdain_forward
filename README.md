# Mirdain Forward

Licensed under the [GNU General Public License v3.0](LICENSE) (GPL-3.0-only).
Third-party components retain their respective licenses.

Minimal Qt C++ menu bar / system tray app. It listens on **127.0.0.1:18744**,
downloads a `.mirdain` archive, and sends the selected slice's **plate 0** to an
ELEGOO, PrusaLink, Bambu LAN, or Klipper (Moonraker) printer, then requests printing. No queue.
Printer names, IDs, and settings are saved locally; printer credentials arrive in
each request.

On launch Forward opens its window: onboarding on first launch, the slicer after.
The menu starts with Open Slicer, Add Printer…, and the organization, then lists the
organization's Forward printers, followed by Check for Updates, Preferences, the
current transfer status, and Quit. Only one
transfer runs at a time. Success means the printer accepted the print request, not that the
first layer has begun. Busy printers are not interrupted. Start commands are
never automatically retried; after an unconfirmed start, check the printer
before sending again.

ELEGOO, PrusaLink, and Moonraker receive the plain `.gcode` inside the native plate package;
Bambu receives the original `.gcode.3mf`. Temporary downloads are deleted after
each transfer. Archive members are read without extracting a directory tree.
Long menu status messages are ellipsized; the tray tooltip retains the full text.

## Onboarding and Forward's pages

`web/` is a React app (Vite, TypeScript) that CMake builds into a single
`index.html` (script and styles inlined) and embeds as the `:/web/index.html` Qt
resource; the build needs Node.js and npm. `src/app/SetupServer.cpp` serves it at
`http://127.0.0.1:18744/`, with Forward's fonts at `/fonts/`, and answers the
`/api/app/*` routes it calls. Those routes accept only Forward's own origin
(`127.0.0.1:18744` or `localhost:18744`, checked against `Host` and `Origin`).

On first launch the window shows onboarding:

1. **Preferences**: launch at sign-in, plate notifications, and automatic update
   checks. **Preferences…** in the menu reopens this page at `#/preferences`.
2. **Sign in**: Forward opens Cognito's hosted sign-in in the default browser
   (authorization code with PKCE, redirected to `http://localhost:18744/callback`),
   and the page polls `/api/app/state` until Forward has exchanged the code.
   `src/app/Account.cpp` keeps the refresh token in `printers.json` and calls the
   Mirdain API with it, so the pages never see a token and the API needs no CORS
   for localhost.
3. **Organization**: choosing one calls `SetupServer::switchOrganization`, which
   loads the organization's slicer printers that have a connection
   (`overrides.mirdain_connection`) and replaces the menu's printers with them.
   Forward refreshes them on each launch. **Organization: …** in the menu changes it later.
4. **Get started**: Add a printer (highlighted) or open the slicer.

Add Printer (`web/src/AddPrinter.tsx`, also **Add Printer…** in the menu) is
copied from Aperture's `ForwardSetup.tsx` without its Verify Forward step, along with
`SelectPicker`, `PrinterPresetDialog`, the setup styles, and the Order Parts step
animation. Forward's menu opens `#/add-printer` and `#/organization`.

For hot reload, run Forward, then `npm run dev` in `web/` and open
`http://127.0.0.1:5174/`; Vite proxies `/api/app` and `/fonts` to Forward. Test
builds can point at another API with `-DFORWARD_API_URL=…`.

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
while it is already running opens the slicer window: macOS sends the running app a
reopen event, and on other platforms the second instance posts `/show` to the running
one before exiting.

`AppPreferences` owns the preferences that the preferences page reads and saves. `StartupRegistration`
and `NotificationOverlay` provide platform-specific behavior behind shared class
interfaces. `AppConfig` reads and atomically writes sections of the existing
`printers.json`, preserving both printer settings and app preferences:

```json
{
  "printers": {},
  "preferences": {
    "onboarding_complete": true,
    "launch_at_sign_in": false,
    "notifications_enabled": false
  },
  "account": {
    "refresh_token": "…",
    "email": "maker@example.com",
    "organization": {"id": 1, "name": "Workshop"}
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
offers **Automatically check for updates**, initially off.
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

Everything goes through `Notifications::post` (`src/app/Notifications.h`). A
notification has an ID (posting the same ID updates it in place), a reason, a title
and body, an indicator (none, spinner, progress bar, done, or failed), a timeout in
milliseconds or `-1` to stay until dismissed, and click and dismiss handlers.

On macOS, notifications appear in Forward's own card under the menu bar icon
(`platform/macos/NotificationOverlay.mm`), with a notch pointing at the icon: one
row per notification, its indicator, title, and body. A row's × dismisses it, and
clicking it elsewhere activates it. Forward no longer uses macOS's notification
center, so it never asks for notification permission.

Windows and Linux show `QSystemTrayIcon::showMessage()` notifications: title and
body only, and only for reasons worth interrupting for there (a plate sent or saved
to USB, a failed plate, a failed Gwaith run). Progress and finished Gwaith runs are
omitted, since those notifications can't update in place or open a run.

With plate notifications on in Preferences, each transfer is one notification: a
spinner following the transfer's progress ("Downloading the plate…", the printer's
own steps, saving and ejecting a USB drive), then **Print requested**, **Saved to
USB**, or the failure. Plate notifications close themselves 8 seconds after the
transfer ends; Gwaith runs stay until dismissed.

## Gwaith auto-slice

Models dropped on a printer in the drop panel (macOS) go to Gwaith, which slices them in
the background (`src/gwaith/`). After the drop, the panel asks for optional notes for
Gwaith (Return or **Slice** sends them, **Skip** sends none, and × or Esc cancels the
drop). More models dropped on the notes card join the drop, which then reads "3 files". Each drop is a
`GwaithAutoSlice`: it uploads the models to a new Gwaith conversation, asks Gwaith in
plain text to slice them for that Forward printer with as few questions as possible and
to export a `.mirdain` before finishing, adding the notes, and follows the conversation over Gwaith's socket (reconnecting and
replaying the history if it drops). It reports Gwaith's current step, the slicing
session Gwaith created (cleared again if Gwaith disconnects from it), and the
last `.mirdain` Gwaith attached.

`GwaithRuns` shows each run as a notification. When Gwaith finishes, it downloads that
`.mirdain`, loads the printer's connection from the main API for this send only, and
sends the plate like **Send and print**, with the transfer's progress in the run's row.
With no `.mirdain` or no printer, the row shows **!** and says why, asking the user to
open the run in the slicer while its session is still open. A run's row is clickable
only while it has a session: clicking opens that session and its chat in the slicer.
Before Gwaith creates one, or after it disconnects from it (ending it), the row shows no
hover or hand cursor and ignores clicks. **Gwaith Activity…** in the menu shows dismissed runs again.
`FORWARD_GWAITH_URL` (CMake) sets the Gwaith service; `./build.sh dev` uses
`http://localhost:8001`.

On Windows, right-clicking an `.stl` or `.3mf` in Explorer offers **Send to Gwaith**
with one entry per Forward printer (on Windows 11, under **Show more options**).
Forward writes that menu to `HKCU\Software\Classes` while signed in to an
organization, rewriting it whenever its printers change, and removes it when signed
out; the installer deletes it on uninstall (`platform/windows/ShellMenu.cpp`). An
entry launches `mirdain_forward.exe --gwaith-printer <id> "<file>"`: a running
Forward takes the file over through `/show`, and each selected file starts its own
run, without the macOS notes step.

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
  platform/             Startup, notification, updater, and USB drive interfaces
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
to `src/`, such as `platform/NotificationOverlay.h` and `printers/PrinterSender.h`.

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
setting, while PrusaLink and Moonraker inherit the default unsupported capability.

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
macOS uses ad-hoc signing. Windows release builds are signed once Artifact Signing
is configured ([setup](packaging/updates/README.md#windows-code-signing)); other Windows builds are unsigned. This
workflow builds and packages only; it does not run tests or publish releases.
The separate **Release Forward** workflow handles version tags, update signing,
and permanent GitHub Release downloads after one-time configuration. Website XML
feeds are maintained separately using the published `signatures.json`.
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

### Save to USB

`POST http://127.0.0.1:18744/usb` with `{"url": "…", "download_headers": {"Authorization": "Bearer …"}}`
downloads one plate file (the slicer's `.gcode` or `.gcode.3mf` plate output, named
by its `Content-Disposition` filename) and copies it to the root of the first
mounted, writable USB drive or SD card (`src/platform/*/RemovableDrives.cpp`:
DiskArbitration on macOS, `DRIVE_REMOVABLE` on Windows, `/media` or `/run/media`
on Linux). Existing files are kept; the copy gets a `-2`, `-3`, … suffix.

After saving, Forward ejects the drive (`diskutil eject` on macOS, a volume
lock/dismount/eject on Windows, `udisksctl` unmount and power-off on Linux).

Success is HTTP 200 with `{"ok":true,"filename":"…","drive":"…","path":"…","ejected":true}`.
If the file was saved but the eject failed (for example, another app has the drive
open), `ejected` is `false` and `eject_error` gives the reason.
With no drive connected it returns 404 with `{"code":"no_usb","error":"…"}`
before downloading. It shares the one-transfer limit with `/send` (409), and
download or write failures return 502 with `{"error":"…"}`.

### Loaded filaments

`POST http://127.0.0.1:18744/filaments` with `{"connection": {…}, "printer_model": "Bambu Lab P1S"}`
reads the printer's loaded spools (bridge method `filaments` in the slicer window). Only
`bambu_lan` is supported; other protocols return 400 with `{"code":"unsupported","error":"…"}`.
Forward connects over LAN MQTT, requests `pushall`, and answers from the full status report
(`src/printers/BambuFilaments.cpp`, a port of `mirdain_printer_client`'s `_filament_units`).
`printer_model` is optional; it supplies extruders for P1P/P1S/X1C firmware that omits AMS info.
It does not count as a transfer, so it runs during one.

Success is HTTP 200 with mirdain_server's `PrinterFilamentUnit` list, external spools first:

```json
{"ok":true,"filaments":[
  {"id":null,"filaments":[{"slot_id":"255","material_id":"GFL99","color":"#FFFFFF","extruder":0}]},
  {"id":"0","ams_type":1,"filaments":[{"slot_id":"0","material_id":"GFA00","color":"#161616","extruder":0}]}
]}
```

`material_id` is Bambu's `tray_info_idx` (`null` when unset) and `color` is `#RRGGBB` or `null`.
Connection failures and a 15-second timeout return 502 with `{"error":"…"}`.

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

### Klipper (`moonraker`)

```json
{"protocol":"moonraker","host":"10.0.0.223","port":7125}
```

The port defaults to Moonraker's 7125; use the Mainsail or Fluidd port (often 80)
when only the front end's proxy is reachable. `api_key` is optional and is sent as
`X-Api-Key` for installations with authorization enabled. Forward requires Klipper's
`ready` state and an idle `print_stats` state (`standby`, `complete`, `cancelled`,
or `error`), then uploads the G-code to the `gcodes` root with `print=true`.
Upload names keep at most 48 characters of the original stem plus a short UUID,
so a send never overwrites an existing file. Success requires Moonraker's
`print_started: true`; a queued upload is reported as a failure. Leveling follows
the sliced G-code and the printer's macros. See
[FORWARD_SUPPORT.md](FORWARD_SUPPORT.md) for brand caveats and
[Moonraker's file upload API](https://moonraker.readthedocs.io/en/latest/web_api/#file-upload).

`GET http://127.0.0.1:18744/health` returns
`{"ok":true,"service":"mirdain_forward","version":"…"}` without contacting a
printer or starting a transfer. The Forward download page uses this endpoint
during guided setup. It must receive this response before saving the printer
profile and forwarding the user to the slicer. Older releases without this
endpoint need to be updated before completing guided setup.

The server handles CORS preflights for `https://slicer.mirdainsystems.com`,
`https://mirdainsystems.com`, `https://www.mirdainsystems.com`, and local development
on `localhost` or `127.0.0.1` at ports 3000 and 5173. Other browser origins are
rejected. Requests without an Origin header, such as native clients, are allowed.
The server binds only to IPv4 loopback. The browser may request **loopback/local
network access**; CORS between the website and this app is handled here, and
the app's native requests to the printer are not subject to browser CORS.

The web slicer's **Send and print** button uses `/send` directly without
availability polling; an unreachable app causes the send request to fail. Browser integration:

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
