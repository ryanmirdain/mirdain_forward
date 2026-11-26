# Forward printer support

Which printer brands and connection protocols Mirdain Forward can send prints to,
and which were investigated and set aside. Brands come from the Orca printer
presets served by `mirdain_server` (`app/slicing_presets/orca/profiles`).

None of the senders added below were tested against real printer hardware. Each
was implemented from the vendor's or project's published API documentation and
open-source client code, compiled, and type-checked only.

## Supported protocols

| Protocol (`connection.protocol`) | Brands | Sender | Added |
| --- | --- | --- | --- |
| `elegoo_sdcp` | ELEGOO (Centauri Carbon and other SDCP printers) | `src/printers/ElegooSender.cpp` | Before this log |
| `prusalink` | Prusa (MK3.5, MK4/S, MINI, XL, CORE One) | `src/printers/PrusaSender.cpp` | Before this log |
| `bambu_lan` | Bambu Lab | `src/printers/BambuSender.cpp` | Before this log |
| `moonraker` | Voron, RatRig, Sovol, Qidi, Vzbot, FLSun, Kingroon, TwoTrees, Mellow, Vivedino, Positron3D, Artillery, ELEGOO Neptune 4, Snapmaker U1, Custom Klipper | `src/printers/MoonrakerSender.cpp` | 2026-11-26 |
| `octoprint` | Lulzbot, Wanhao, Creality (Ender/CR), Anycubic, Geeetech and any other brand except Bambu Lab, through a USB-attached OctoPrint server | `src/printers/OctoPrintSender.cpp` | 2026-11-26 |
| `flashforge_lan` | Flashforge Adventurer 5M, 5M Pro, AD5X | `src/printers/FlashforgeSender.cpp` | 2026-11-26 |
| `creality_lan` | Creality K1, K1C, K1 SE, K1 Max, K2, K2 Plus, K2 Pro (stock firmware) | `src/printers/CrealitySender.cpp` | 2026-11-26 |
| `duet` | SeeMeCNC, Meltingplot, CONSTRUCT3D, Vivedino Troodon (RRF), RatRig (Duet builds), Custom RRF | `src/printers/DuetSender.cpp` | 2026-11-26 |
| `snapmaker_http` | Snapmaker 2.0 A150, A250, A350 (U1 through `moonraker`) | `src/printers/SnapmakerSender.cpp` | 2026-11-26 |
| `ultimaker_api` | UltiMaker S5 (and other S-line printers) | `src/printers/UltimakerSender.cpp` | 2026-11-26 |

### Cross-repository work for new protocols

Forward accepts a new protocol in its local `/print` endpoint and in saved
printers as soon as its sender exists. To add such a printer through Forward's
Add Printer page or send to it from the web slicer, the protocol also has to be
accepted outside this repository:

- `mirdain_server/app/schemas.py`: a `SlicerPrinterConnection` subclass for the
  protocol in `ClientSlicerPrinterCreate.connection` (otherwise saving returns 422).
- `mirdain_web_slicer/src/features/printers/printerConnection.ts`: the protocol in
  `PrinterConnection` and `printerConnection()`.
- `mirdain_chat_ui/src/printer-presets/CustomPrinterDialog.tsx` (and its vendored
  copies) if the slicer's own printer dialog should offer it.

Pending for: `moonraker`, `octoprint`, `flashforge_lan`, `creality_lan`, `duet`, `snapmaker_http`, `ultimaker_api`.

## Caveats by brand

- **Bambu Lab** (`bambu_lan`): AMS slots are not derived from the slice. The request
  must carry `use_ams` and an `ams_mapping` in the plate's filament order; the
  default prints from the external spool. Firmware that requires signed commands
  needs LAN Only mode with Developer Mode.
- **Klipper printers** (`moonraker`):
  - Bed leveling, nozzle wipes, and similar steps come from the sliced start G-code
    and Klipper macros; Forward has no auto-level setting for Moonraker.
  - Moonraker must be reachable on port 7125, or on the Mainsail/Fluidd port (often
    80) when that front end proxies the API. Installations with authorization
    enabled need an API key unless this computer's address is in
    `trusted_clients`.
  - If Moonraker's job queue is enabled and decides to queue the upload, Forward
    reports a failure instead of success; the print may still start later.
  - **Sovol**: only the Klipper models (SV07, SV08, SV08 MAX, Zero) run Moonraker.
    SV01/SV02/SV05/SV06 (stock Marlin) need Klipper installed, or OctoPrint.
  - **Qidi**: X-Plus 3/4, X-Max 3/4, Q1 Pro, Q2 and Plus 4 run Moonraker. Older
    X-Plus/X-Max/X-CF Pro use Qidi's own firmware and are not supported.
  - **ELEGOO**: Neptune 4 series runs Moonraker; Centauri Carbon uses SDCP.
  - **FLSun**: S1, T1, and V400 run Klipper. Q5, QQ-S Pro, and SR are Marlin.
  - **Artillery**: Sidewinder X4 and M1 Pro run Klipper; older Genius, Hornet, and
    Sidewinder X1/X2/X3 are Marlin.
  - **Kingroon**: KLP1 runs Klipper; KP3S models are Marlin.
  - **Creality**: not offered for Moonraker. Stock K1/K2 firmware does not expose a
    usable Moonraker without rooting; use `creality_lan` (below).

- **OctoPrint printers** (`octoprint`), the fallback for Marlin brands such as
  Lulzbot, Wanhao, LONGER, Geeetech, Tronxy, Voxelab, Comgrow, and Folgertech:
  - Requires a separate OctoPrint server (usually a Raspberry Pi running OctoPi)
    connected to the printer over USB and to the printer in OctoPrint's UI. Forward
    reports a failure when OctoPrint is not connected to the printer; it never
    opens the serial connection itself.
  - G-code streams over USB serial, so print quality on fast printers depends on
    the host; this is OctoPrint's limitation, not Forward's.
  - An API key is required. Leveling follows the sliced start G-code.
  - Offered for every vendor except Bambu Lab, including brands with a native
    protocol (Prusa, ELEGOO, Klipper brands), where it is listed after the native
    method.
- **Flashforge** (`flashforge_lan`):
  - LAN mode must be enabled on the printer, which shows the check code. The
    serial number and check code authenticate every request.
  - AD5X material station (IFS): Forward always sends `useMatlStation=false`,
    `gcodeToolCnt=0`, and an empty material mapping, so prints are single-filament.
    Multi-color AD5X prints are not supported; slot mappings are not derived from
    the slice.
  - Only the newer HTTP API (port 8898) is implemented. Adventurer 3/4, Guider,
    Creator, and Artemis models that only speak Flashforge's legacy TCP G-code
    protocol on port 8899 are not supported natively; use OctoPrint where the
    printer allows USB printing.
  - Busy detection uses `/detail` status strings `ready`, `completed`, and
    `cancel` as idle. These come from community clients, not a published
    specification; other states are reported as busy.
  - Auto-leveling follows the saved setting (`levelingBeforePrint`); flow
    calibration, first-layer inspection, and time-lapse are disabled.
- **Creality** (`creality_lan`):
  - Only the Wi-Fi models on Creality's Linux firmware (K1 and K2 families, and
    likely Hi and SPARKX i7) have this interface. Ender, CR, and Sermoon models
    without networking need OctoPrint.
  - CFS multi-color: Forward always sends a plain `opGcodeFile` start, so the
    printer feeds from the spool holder or the currently loaded CFS slot. CFS slot
    mappings (`colorMatch`/`multiColorPrint`) are not derived from the slice.
  - The G-code path depends on the model from `/info`: K1-family uses
    `/usr/data/printer_data/gcodes/`, others `/mnt/UDISK/printer_data/gcodes/`.
    An unrecognized new K1 variant would get the wrong path and fail to start.
  - Busy and printing detection use the WebSocket status `state` values (1 printing,
    5 paused) known from community clients; there is no published specification.
  - The start has no acknowledgment. Forward reports success when the status
    turns to printing or, as K1 firmware does after accepting a start, the
    WebSocket closes. Otherwise it times out after 30 seconds.
  - No auto-level setting: leveling follows the printer's own start routine and
    the sliced G-code.
- **Duet / RepRapFirmware** (`duet`):
  - RepRapFirmware 3 or newer is required: the status check uses the object model
    (`rr_model` standalone, `/machine/status` with an SBC). RRF 2 boards report a
    failure rather than being sent to blind.
  - Standalone boards allow a few HTTP sessions; Forward does not call
    `rr_disconnect`, so its session expires on the board's idle timeout. A board
    with every session in use (many Duet Web Control tabs) refuses the connection.
  - `rr_gcode` only queues `M32`, so success is the status turning to
    `processing` within 20 seconds. A very short file that finishes before the
    first poll would be reported as a failure.
  - Files are stored on the board's SD card under `0:/gcodes/`, which must have
    space. Leveling follows the sliced G-code and `start.g`.
  - **RatRig**: V-Core 3 machines ship with either a Duet (RRF) or Klipper; both
    methods are offered, Moonraker first.
  - **SeeMeCNC**: older RostockMAX v3.2 boards (RAMBo/Marlin) are not Duet; use
    OctoPrint for those.
- **Snapmaker** (`snapmaker_http`, and `moonraker` for the U1):
  - Each new connection token must be approved on the touchscreen within 60
    seconds. Forward keeps the approved token in memory for the printer, so
    approval is needed again after Forward restarts or the printer forgets it.
    A known token can be supplied as `connection.token` to skip the prompt.
  - Only 3D printing (`type=3DP`) is sent; laser and CNC modules are out of scope,
    and the printer must have the 3D printing head attached.
  - `start_print` returning HTTP 200 is treated as success; the API returns no
    further confirmation. Idle means status `IDLE`, `FINISHED`, or `STOPPED`.
  - The endpoints follow Snapmaker Luban and community clients; Snapmaker does not
    publish this API. Dual-extruder (A250/A350 Dual) prints use whatever tool the
    sliced G-code selects.
  - **J1 and Artisan** are not supported: they use Snapmaker's binary SACP protocol
    instead of the HTTP API. See the investigation log.
  - **U1** runs Klipper; use `moonraker`.
- **UltiMaker** (`ultimaker_api`):
  - Forward must be allowed once on the touchscreen within 60 seconds. The
    approved id and key are kept in memory, so approval is needed again after
    Forward restarts unless `auth_id` and `auth_key` are supplied.
  - The printer must be `idle`. After a finished print it waits for the build
    plate to be cleared and confirmed on the touchscreen; until then it is busy.
  - The G-code needs the Griffin header, which the Orca S5 preset writes. Material
    and print-core checks are the printer's own: a mismatch with the loaded
    material is shown on the touchscreen and may need confirmation before
    printing starts.
  - HTTP 201 for the print job is the only confirmation; Forward does not poll
    the job afterwards.
  - **UltiMaker 2, 2+, 2 Extended+** have no network interface (the 2+ Connect is
    not in the presets); use OctoPrint, which is still offered for UltiMaker.

## Investigation log

One brand and protocol per entry, newest last.

### 2026-11-26 · Voron (and other Klipper printers) · Moonraker · Implemented

Voron printers have no vendor firmware: they run Klipper with Moonraker and a
Mainsail or Fluidd front end. Moonraker's HTTP API is documented
([web API](https://moonraker.readthedocs.io/en/latest/web_api/)) and stable.
The sender:

1. `GET /printer/info` and requires Klipper `state` to be `ready`.
2. `GET /printer/objects/query?print_stats=state` and requires `standby`,
   `complete`, `cancelled`, or `error`, so a running or paused job is never
   interrupted.
3. `POST /server/files/upload` (multipart, `root=gcodes`, `print=true`) with a
   unique filename, and succeeds only when the response reports
   `print_started: true`.

An optional `api_key` is sent as `X-Api-Key`. The same sender covers every
Klipper brand listed above. Add Printer now picks connection methods from a table
of vendors, so ELEGOO offers both SDCP and Moonraker.

### 2026-11-26 · Lulzbot (and other Marlin printers) · OctoPrint · Implemented

Lulzbot's TAZ and Mini printers, like most Marlin brands in the preset catalog
(Wanhao, LONGER, Geeetech, Tronxy, older Creality and Anycubic), have no network
interface of their own; Orca's presets for 123 machines name `octoprint` as the
host type. OctoPrint's REST API is documented
([files](https://docs.octoprint.org/en/master/api/files.html),
[printer](https://docs.octoprint.org/en/master/api/printer.html)). The sender:

1. `GET /api/version` with `X-Api-Key` to check the server and key.
2. `GET /api/printer` and requires the `operational` and `ready` flags without
   `printing`, `paused`, `pausing`, `cancelling`, or `error`; HTTP 409 means
   OctoPrint is not connected to the printer.
3. `POST /api/files/local` (multipart, `select=true`, `print=true`) with a unique
   filename, and succeeds only when the response's `effectivePrint` is true.

### 2026-11-26 · Flashforge · LAN HTTP API · Implemented

Flashforge's Adventurer 5M, 5M Pro, and AD5X expose an HTTP API on port 8898 once
LAN mode is enabled, authenticated by the serial number and check code. It has
no official public specification; the request shapes follow OrcaSlicer's
Flashforge host (read for protocol details only, no code copied) and community
clients. The sender:

1. `POST /detail` with `{"serialNumber","checkCode"}`, requires `code` 0 and an
   idle `detail.status`.
2. `POST /uploadGcode` (multipart field `gcodeFile`) with `serialNumber`,
   `checkCode`, `fileSize`, `printNow: true`, `levelingBeforePrint`, and the
   material-station headers in HTTP headers, and succeeds only on `code` 0.

The legacy port 8899 TCP protocol (`~M601`, `~M28`, `~M29`, `~M23`) used by older
models is a separate candidate and was not implemented in this entry.

### 2026-11-26 · Creality · Creality Print LAN (HTTP + WebSocket) · Implemented

Creality's networked K1/K2 printers accept uploads at `POST http://host/upload/<name>`
(multipart field `file`) and take commands as JSON over a WebSocket on port 9999,
the interface Creality Print uses. There is no public specification; details
follow OrcaSlicer's CrealityPrint host (read for protocol only, no code copied)
and community clients. The sender:

1. `GET /info` to read the model and pick the G-code directory.
2. Opens the WebSocket, asks for status (`reqPrinterPara`), answers `heart_beat`
   with `ok`, and refuses to send when `state` is 1 or 5.
3. Uploads the G-code with a unique filename.
4. Reopens the WebSocket and sends
   `{"method":"set","params":{"opGcodeFile":"printprt:<path>"}}`, succeeding on a
   printing status or the K1 post-start disconnect.

Rooted K1/K2 printers that run Moonraker can alternatively use `moonraker`.

### 2026-11-26 · SeeMeCNC (and other Duet printers) · RepRapFirmware HTTP · Implemented

SeeMeCNC's Artemis, BOSSdelta, and RostockMAX v4 presets (28), Meltingplot, and
CONSTRUCT3D name the `duet` host type. RepRapFirmware's HTTP API is documented
([standalone](https://github.com/Duet3D/RepRapFirmware/wiki/HTTP-requests),
[DSF](https://github.com/Duet3D/DuetSoftwareFramework#rest-api)). The sender:

1. `GET /rr_connect?password=…&time=…` (`err` 0 required; a `sessionKey` is sent
   back as `X-Session-Key`). If the request fails, it tries an SBC with
   `GET /machine/connect?password=…`.
2. Reads `state.status` and requires `idle`.
3. Uploads to `0:/gcodes/` with `POST /rr_upload?name=…` (`err` 0) or
   `PUT /machine/file/…`.
4. Sends `M32 "0:/gcodes/<file>"` through `rr_gcode` or `POST /machine/code`, then
   polls status once a second until it reads `processing`.

### 2026-11-26 · Snapmaker · Snapmaker 2.0 HTTP API · Implemented (A-series)

The Snapmaker 2.0 A-series serves an HTTP API on port 8080, as used by Snapmaker
Luban and Cura's Snapmaker plugin. The sender:

1. Reuses an approved token, or `POST /api/v1/connect` for a new one and polls
   `GET /api/v1/status?token=…` every 2 seconds (204 awaiting approval, 401
   declined, 200 approved) for up to 60 seconds.
2. Requires `status` `IDLE`, `FINISHED`, or `STOPPED`.
3. `POST /api/v1/prepare_print` (multipart `token`, `type=3DP`, `file`), then
   `POST /api/v1/start_print` with the token.

The Snapmaker U1 is a Klipper printer and was added to the Moonraker vendors. The
J1 and Artisan use SACP, a binary protocol over TCP that Luban implements but
Snapmaker does not document; it is left as a separate candidate.

### 2026-11-26 · UltiMaker · S-line local HTTP API · Implemented

The S-line's local API (`/api/v1`, documented on the printer at `/docs/api/`)
is what Cura uses on the LAN. The sender:

1. Verifies remembered credentials with `GET /api/v1/auth/verify` (Digest), or
   requests new ones with `POST /api/v1/auth/request` and polls
   `GET /api/v1/auth/check/<id>` until `authorized` (60 seconds at most).
2. Requires `GET /api/v1/printer/status` to be `"idle"`.
3. `POST /api/v1/print_job` (multipart `file`, Digest auth), succeeding on HTTP 2xx;
   the printer starts the job itself.
