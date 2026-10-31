# Mirdain Forward

Minimal Qt C++ menu bar / system tray app. It listens on **127.0.0.1:18744**,
downloads a `.mirdain` archive, and sends the selected slice's **plate 0** to an
ELEGOO, PrusaLink, or Bambu LAN printer, then requests printing. No account,
saved configuration, or queue. Printer credentials arrive in each request.

The menu contains the current transfer status and Quit. Only one transfer runs
at a time. Success means the printer accepted the print request, not that the
first layer has begun. Busy printers are not interrupted. Start commands are
never automatically retried; after an unconfirmed start, check the printer
before sending again.

ELEGOO and PrusaLink receive the plain `.gcode` inside the native plate package;
Bambu receives the original `.gcode.3mf`. Temporary downloads are deleted after
each transfer. Archive members are read without extracting a directory tree.
Long menu status messages are ellipsized; the tray tooltip retains the full text.

## Adding printer protocols

`PrinterSender` is the shared interface. `fileFormat()` selects plain G-code or
the native plate archive. `send(PrinterFile)` receives the chosen bytes,
filename, bed type, and embedded G-code path/checksum. Implementations emit
`progress`, `completed` when the print request is accepted, or `failed`.
Each HTTP request gets a new sender.

`ElegooSender`, `PrusaSender`, and `BambuSender` own their protocol's connection
validation, transfer, and print request. To add another protocol, subclass
`PrinterSender`, add its sources to CMake, and add a branch to
`PrinterSender::create(connection)`. The HTTP handler, archive download,
plate selection, tray status, and response flow are shared.

## Build and run

Requires Qt 6.8+ with Widgets, Network, HTTP Server, WebSockets, and MQTT, CMake,
a C/C++17 compiler, and `curl` with FTPS support. CMake downloads and statically
links a pinned miniz 3.1.2 source archive on first configure; no external unzip
tool is needed. Dependency examples, fuzzers, and tests are disabled.
Bambu uploads run curl as a subprocess; credentials go through stdin rather than
process arguments. macOS uses `/usr/bin/curl`; Windows uses the operating system's
`System32/curl.exe`; Linux finds curl on PATH.

### macOS

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
open build/mirdain_forward.app
```

On macOS, every link automatically bundles Qt's frameworks and plugins and
ad-hoc signs the app. No separate `macdeployqt` command is needed. Stale deployed
files are removed before linking, so rebuilding cannot mix bundled and Homebrew
Qt libraries. `LSUIElement` keeps the app in the menu bar without a Dock icon.
Allow local network access if macOS requests it.

### Windows

Install Visual Studio 2022 C++ build tools and Qt's MSVC 2022 x64 kit, including
the HTTP Server, WebSockets, and Image Formats modules. Build and install the
matching Qt MQTT module from source as shown in the workflow below; it is not
included in the public SDK binaries. From a Visual Studio developer PowerShell
prompt (adjust the Qt path for your installation):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.9.2/msvc2022_64
cmake --build build --config Release --parallel
.\build\Release\mirdain_forward.exe
```

Each link runs `windeployqt` to copy Qt DLLs, plugins, and compiler runtime files
beside the executable. Distribute the whole `build/Release` folder. The executable
uses the supplied app icon and runs in the system tray without a console window.
Windows 10/11 supplies curl; printer credentials and the localhost API work the
same way as on macOS.

On Linux, use your Qt installation's CMake prefix and run `build/mirdain_forward`
in a desktop environment with a system tray and curl installed.

## GitHub Actions builds

The **Build apps** workflow in `.github/workflows/build.yml` runs on pushes to
`main`, pull requests, and manual **Actions > Build apps > Run workflow** runs.
It uses pinned action revisions and Qt 6.9.2, caches the SDK, and builds:

| Artifact | Runner | Contents |
| --- | --- | --- |
| `mirdain_forward-macos-arm64` | `macos-15` | Apple Silicon `.app` with bundled Qt |
| `mirdain_forward-macos-x86_64` | `macos-15-intel` | Intel `.app` with bundled Qt |
| `mirdain_forward-windows-x64` | `windows-2022` | `.exe`, Qt DLLs, plugins, and compiler runtime |

Qt MQTT is built from the pinned Qt 6.9.2 source revision and installed into the
CI SDK because the public SDK catalog does not provide MQTT binaries. It uses
Ninja with a single Release configuration; build and install are separate steps
so installation only runs after a successful build. Its tests and examples are
disabled. Windows ZIPs also include the MSVC runtime DLLs
beside the executable so they can run without a separate runtime installation.

Once the workflow file is pushed, open a completed run's **Artifacts** section
to download the ZIP for your platform. Artifacts are kept for 14 days. macOS ZIPs
preserve the app bundle's symlinks and permissions. The builds include the README
and miniz/Qt MQTT licenses. macOS uses ad-hoc signing; Windows builds are unsigned. This
workflow builds and packages only; it does not run tests or publish releases.

## Request

`POST http://127.0.0.1:18744/send` with `Content-Type: application/json`:

```json
{
  "url": "https://example.com/download/part.mirdain",
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
uploaded `/local/` path. Automatic bed leveling is enabled, time-lapse is disabled,
and platform type comes from the slice's bed type (textured versus smooth/cool).
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
Legacy PrusaLink uses multipart upload with `select=true` and `print=true`,
defaulting to `local` storage. Success requires a successful HTTP response to
upload-and-print. Leveling follows the sliced G-code's start sequence.

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

The app checks the MQTT status, uploads the native `.gcode.3mf` to the storage
root over FTPS, checks the printer is still idle, then publishes `project_file`
with the archive's actual `Metadata/plate_N.gcode` path and G-code MD5.
Success requires the matching MQTT command acknowledgment, not just successful
upload or MQTT delivery. The start request enables bed leveling and disables
time-lapse, flow calibration, vibration calibration, and layer inspection.

By default it uses the external spool (`use_ams=false`, `ams_mapping=[254]`).
For AMS, add `"use_ams":true,"ams_mapping":[0]`: provide one global slot ID per
sliced filament (`AMS index * 4 + slot index`, both zero-based; `-1` for unused).
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
  }),
})
const result = await response.json()
if (!response.ok) throw new Error(result.error)
```
