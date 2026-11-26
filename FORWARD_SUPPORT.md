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
| `moonraker` | Voron, RatRig, Sovol, Qidi, Vzbot, FLSun, Kingroon, TwoTrees, Mellow, Vivedino, Positron3D, Artillery, ELEGOO Neptune 4, Snapmaker U1, Anycubic with Rinkhals, Custom Klipper | `src/printers/MoonrakerSender.cpp` | 2026-11-26 |
| `octoprint` | Lulzbot, Wanhao, Creality (Ender/CR), Anycubic, Geeetech and any other brand except Bambu Lab, through a USB-attached OctoPrint server | `src/printers/OctoPrintSender.cpp` | 2026-11-26 |
| `flashforge_lan` | Flashforge Adventurer 5M, 5M Pro, AD5X | `src/printers/FlashforgeSender.cpp` | 2026-11-26 |
| `creality_lan` | Creality K1, K1C, K1 SE, K1 Max, K2, K2 Plus, K2 Pro (stock firmware) | `src/printers/CrealitySender.cpp` | 2026-11-26 |
| `duet` | SeeMeCNC, Meltingplot, CONSTRUCT3D, Vivedino Troodon (RRF), RatRig (Duet builds), Custom RRF | `src/printers/DuetSender.cpp` | 2026-11-26 |
| `snapmaker_http` | Snapmaker 2.0 A150, A250, A350 (U1 through `moonraker`) | `src/printers/SnapmakerSender.cpp` | 2026-11-26 |
| `ultimaker_api` | UltiMaker S5 (and other S-line printers) | `src/printers/UltimakerSender.cpp` | 2026-11-26 |
| `dremel_lan` | Dremel 3D45, 3D40 | `src/printers/DremelSender.cpp` | 2026-11-26 |
| `esp3d` | Volumic, M3D, and other Marlin printers with an ESP3D Wi-Fi module | `src/printers/Esp3dSender.cpp` | 2026-11-26 |
| `mks_wifi` | CoLiDo, Flying Bear, Two Trees, and other printers with an MKS Robin Wi-Fi module | `src/printers/MksSender.cpp` | 2026-11-26 |
| `simplyprint` | iQ (TiQ2, TiQ8) and any non-Bambu printer linked to SimplyPrint | `src/printers/SimplyPrintSender.cpp` | 2026-11-26 |
| `flashforge_legacy` | Flashforge Adventurer 3/4, Guider, Creator, Artemis | `src/printers/FlashforgeLegacySender.cpp` | 2026-11-26 |
| `qidi_legacy` | QIDI X-Plus, X-Max, X-CF Pro (pre-Klipper) | `src/printers/QidiLegacySender.cpp` | 2026-11-26 |

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

Pending for: `moonraker`, `octoprint`, `flashforge_lan`, `creality_lan`, `duet`, `snapmaker_http`, `ultimaker_api`, `dremel_lan`, `esp3d`, `mks_wifi`, `simplyprint`, `flashforge_legacy`, `qidi_legacy`.

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
    X-Plus/X-Max/X-CF Pro use `qidi_legacy`.
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
  - The HTTP API (port 8898) covers the Adventurer 5M series and AD5X. Older
    Adventurer 3/4, Guider, Creator, and Artemis models use `flashforge_legacy`.
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
- **Dremel** (`dremel_lan`):
  - The 3D20 has no network interface and is not supported (SD card only).
  - The 3D40 traditionally prints Dremel's `.g3drem` format (G-code with a binary
    header and thumbnail). Forward uploads plain `.gcode`, which the 3D45 accepts;
    whether a given 3D40 firmware accepts it is unconfirmed.
  - There is no authentication: anyone on the network can send to the printer,
    and Forward needs only its address.
  - Forward refuses to send while `door_open` is reported as 1. Success is the
    status turning `busy` or the job `building`/`preparing` within 20 seconds.
  - The protocol follows the community `dremel3dpy` library; Dremel does not
    publish it.
- **ESP3D Wi-Fi modules** (`esp3d`; Volumic, M3D):
  - An SD card must be in the printer: ESP3D streams the upload over serial onto
    it (`/upload_serial`), which is slow for large files.
  - Files get generated 8.3 names (`MFxxxxxx.GCO`), because longer names can crash
    Marlin's SD support through ESP3D.
  - The busy check sends `M27`. ESP3D versions that do not relay the printer's
    reply cannot detect a running print, so on those Forward cannot avoid
    interrupting one; only send when the printer is idle.
  - Success means `M23` and `M24` were delivered; ESP3D gives no start
    confirmation. There is no authentication.
  - Only ESP3D 2.x's `/upload_serial` route is used, as in OrcaSlicer; ESP3D 3.x
    setups that store files on the module's own SD card are not supported.
- **MKS Wi-Fi modules** (`mks_wifi`; CoLiDo, Flying Bear Ghost, Two Trees):
  - An SD card (or the board's storage) must be present; uploads go through the
    module and are slow.
  - The busy check sends `M27` over the TCP console (port 8080, `console_port`).
    Firmware that does not answer with Marlin's `SD printing byte` text cannot be
    checked, so a running print could be interrupted.
  - Success means `M23` and `M24` were each answered with `ok`; there is no
    further start confirmation. There is no authentication.
  - Upload names keep the original stem (shortened) rather than 8.3 names, as
    OrcaSlicer does; MKS firmware handles long names, but plain Marlin builds
    behind an MKS module may not.
- **SimplyPrint** (`simplyprint`; iQ TiQ2/TiQ8):
  - This is a cloud route: the G-code goes to SimplyPrint's servers, not straight
    to the printer, and needs internet access on both ends.
  - API file uploads (`files/Upload`) require SimplyPrint's Print Farm plan; an API
    key requires Pro or an organization.
  - `connection.host` is SimplyPrint's API host, `api.simplyprint.io` (Add Printer
    fills it in); uploads go to the matching `files.` host. `company_id`,
    `printer_id`, and `api_key` are required.
  - The printer must be online, `operational`, and not awaiting a bed clear.
    Success is `CreateJob` returning `status: true`; the printer then downloads
    the file from SimplyPrint, so the print may begin a little later.
  - SimplyPrint's docs disagree on whether `CreateJob` also needs
    `select_mode: "file_id"`; Forward sends only `file_id`, as the CreateJob
    reference specifies.- **Flashforge legacy** (`flashforge_legacy`; Adventurer 3/4, Guider, Creator, Artemis):
  - The G-code is streamed as raw 4 KiB writes after `~M28`, as OrcaSlicer does.
    FlashPrint itself wraps data in framed packets on some firmware; printers that
    require that framing will reject or corrupt the upload. This is the largest
    risk in this sender.
  - Busy detection requires `MachineStatus: READY` in the `~M119` reply.
  - The printer takes control only after `~M601 S1`; FlashPrint or another client
    holding the connection can block Forward.
  - Success is `ok` to `~M23`; there is no further start confirmation and no
    authentication. Uploads are slow over the console.
- **QIDI legacy** (`qidi_legacy`; X-Plus, X-Max, X-CF Pro):
  - The protocol is UDP on port 3000 with no authentication. Each 1280-byte block
    waits for `ok` (or `resend <offset>`), with 3 tries of 2 seconds, so uploads of
    large files take a long time.
  - Plain `.gcode` is sent. QIDI's own tools compress to `.gcode.tz` with a closed
    binary tool; uncompressed upload follows the QidiPrint plugin's fallback and
    is unconfirmed on every firmware.
  - No thumbnail (`M4010`) or print-time (`M2100`) lines are added, so the printer
    screen shows no preview or time estimate.
  - Busy detection requires `D:<done>/<total>/1` in the `M4000` reply; the start is
    `M6030 ":<name>" I1` with no further confirmation beyond a reply without
    `Error`.

- **Other Klipper brands** (`moonraker`, offered from 2026-11-26 because their
  Orca presets target Klipper): BIQU Hurakan, BLOCKS, Chuanying X1, Co Print
  ChromaSet, CoLiDo DIY 4.0/SR1, Comgrow T300/T500, Cubicon xCeler, DeltaMaker,
  Eryone Thinker X400, Flying Bear Reborn3, Ginger G1, InfiMech TX, LH Stinger,
  LONGER LK10, MagicMaker BoneKing/hj SK, OpenEYE Peacock V2, Peopoly Magneto X,
  Phrozen Arco, RH3D, Rolohaun Design, SecKit, Volumic EXO/SH Klipper builds,
  Wanhao France SMARTPAD builds, WonderMaker ZR, Z-Bolt, and re3D Gigabot/Terabot.
  Klipper G-code flavor alone does not prove a reachable Moonraker (Tiertime and
  Flashforge also use it), so stock-firmware Moonraker access is unverified for
  these; brands whose stock firmware hides it may need Mainsail/Fluidd installed.
  Their Marlin models use OctoPrint (or ESP3D/MKS where fitted).

## Not supported

| Brand | Protocol investigated | Reason | Logged |
| --- | --- | --- | --- |
| Raise3D (Pro3, Pro3 Plus) | Raise3D Remote Access API (port 10800) | Upload and start-job endpoints are documented only on the printer itself; not confirmable without hardware | 2026-11-26 |
| Tiertime (UP300 HS, UP310 Pro, UP400 Pro, UP600 HS) and Afinia (H+1 HS) | Tiertime network printing (UP Studio) | Closed, undocumented vendor protocol; no public API | 2026-11-26 |
| Anker (AnkerMake M5, M5C) | AnkerMake MQTT + PPPP | Needs Anker cloud account credentials and keys; proprietary encrypted protocols | 2026-11-26 |
| Anycubic (Kobra 3, S1, Kobra 2 series) | Stock LAN mode (local MQTT over TLS) | Undocumented; credentials come from Anycubic Slicer Next and payloads are model-specific | 2026-11-26 |

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

### 2026-11-26 · Raise3D · Remote Access API · Not supported yet

Raise3D's RaiseTouch firmware has a Remote Access API, enabled under
Setting > More Settings > Developer, with its password set in the same place.
[Raise3D's docs](https://support.raise3d.com/RaiseTouch/6-1-7-1-enable-remote-access-api-17-895.html)
say the API reference is served by the printer itself at `http://<ip>:10800`;
there is no public copy. From a community client
([kore.io.raise3d](https://github.com/ptro46/kore.io.raise3d)) only these parts are known:

- Login: `GET /v1/login?sign=<s>&timestamp=<ms>`, where `s` is the lowercase hex
  MD5 of the lowercase hex SHA-1 of `password=<pw>&timestamp=<ms>`; the token is
  `data.token` and is passed to later calls as `?token=`.
- Read-only: `GET /v1/printer/basic`, `/v1/printer/nozzle1`, `/v1/printer/nozzle2`,
  `/v1/job/currentjob` (`data.job_status`).

No public source gives the file upload or start-job endpoints, so a sender would
be guesswork that could only be confirmed against a printer. To add support,
someone with a Pro3 can enable the API, save the reference page at
`http://<printer-ip>:10800`, and implement the upload and start calls from it.
OctoPrint is listed for Raise3D in Add Printer only because it is offered for
every non-Bambu vendor; Raise3D printers are not meant to be driven over USB.

### 2026-11-26 · Dremel · Dremel HTTP command interface · Implemented

Dremel's networked printers (3D45, 3D40) accept plain-text commands at
`POST http://host/command` and uploads at `/print_file_uploads`, as used by the
[dremel3dpy](https://github.com/godely/dremel3dpy) library (Home Assistant's
Dremel integration). The sender:

1. Posts `GETPRINTERSTATUS` and requires `status` `ready` with the door closed.
2. Uploads the G-code as multipart field `print_file` with a short unique name.
3. Posts `PRINT=<name>`, fails on an `error_code` other than 200, then polls
   `GETPRINTERSTATUS` once a second until the job is `building` or `preparing`
   or the printer is `busy`.

### 2026-11-26 · Anycubic · Stock LAN mode · Not supported yet

Recent Kobra 3 and Kobra S1 firmware has a LAN mode that turns off the cloud and
lets Anycubic Slicer Next send prints locally
([Anycubic's LAN guide](https://wiki.anycubic.com/en/fdm-3d-printer/kobra-s1-max-combo/lan-connection-guide)).
Anycubic does not document the protocol. Community Home Assistant integrations
show it is MQTT over TLS on the printer, with credentials that clients take from
Anycubic Slicer Next's local data, plus an HTTP upload. Their authors note that
models besides the ones they own are untested. Without a specification or a
printer, the credential source, topics, and start payload could not be confirmed,
so no sender was written.

What works today:
- **Kobra 2/3/S1 with [Rinkhals](https://jbatonnet.github.io/Rinkhals/)** (community
  firmware) expose Moonraker; Anycubic is offered `moonraker` in Add Printer from
  the next commit on.
- **Marlin models** (i3 Mega, Vyper, Chiron, 4Max, original Kobra) print through
  OctoPrint over USB.

### 2026-11-26 · Volumic and M3D · ESP3D · Implemented

Volumic (6 presets) and M3D (2) name the `esp3d` host type: an ESP8266/ESP32 module
running [ESP3D](https://github.com/luc-github/ESP3D) bridges Wi-Fi to the printer's
serial port. The sender follows OrcaSlicer's ESP3D host (read for protocol only):

1. `GET /command?plain=M27`; a reply containing `SD printing byte` means busy.
2. `POST /upload_serial` (multipart `file`) with an 8.3 filename.
3. After 1.5 seconds, `GET /command?plain=M23 <name>` and then `M24`.

### 2026-11-26 · CoLiDo · MKS Wi-Fi · Implemented

CoLiDo's presets name the `mks` host type: an MKS Robin Wi-Fi module (also on
Flying Bear and Two Trees printers) that accepts uploads over HTTP and G-code over
a TCP console. The sender follows OrcaSlicer's MKS host (read for protocol only):

1. Opens the TCP console on port 8080 and sends `M27`, refusing when the reply
   shows an SD print in progress.
2. `POST /upload?X-Filename=<name>` with the raw G-code body; a JSON `err` other
   than 0 is a rejection.
3. After 1.5 seconds, sends `M23 <name>` and `M24` on the console, each waiting
   for `ok`.

### 2026-11-26 · iQ · SimplyPrint cloud API · Implemented

The iQ TiQ2 and TiQ8 presets name the `simplyprint` host type: these printers
are managed through [SimplyPrint](https://simplyprint.io), whose API is
[publicly documented](https://apidocs.simplyprint.io/). The sender:

1. `GET https://api.simplyprint.io/{company}/printers/Get?pid={printer}` with
   `X-API-KEY`; requires `online`, `state` `operational`, and no
   `awaitingBedClear`.
2. `POST https://files.simplyprint.io/{company}/files/Upload` (multipart `file`)
   and reads `file.id`.
3. `POST https://api.simplyprint.io/{company}/printers/actions/CreateJob?pid=…`
   with `{"file_id": …}`, succeeding on `status: true`.

The same route works for any printer brand already linked to SimplyPrint, so
Add Printer offers it for every vendor except Bambu Lab.

### 2026-11-26 · Anker · AnkerMake MQTT and PPPP · Not supported

AnkerMake M5 and M5C printers have no documented local API. The community
[ankermake-m5-protocol](https://github.com/Ankermgmt/ankermake-m5-protocol)
project (`ankerctl`) reverse-engineered two protocols: encrypted MQTT through
Anker's servers, and PPPP, a UDP peer-to-peer protocol for file transfer. Its
[MQTT notes](https://raw.githubusercontent.com/Ankermgmt/ankermake-m5-protocol/master/documentation/developer-docs/mqtt-overview.md)
show the MQTT login and encryption key come from Anker's HTTPS API using the
account's auth token, which `ankerctl` imports from the official AnkerMake app's
`login.json`. Forward would need to hold Anker account credentials and reimplement
both proprietary protocols, none of which can be checked without a printer, so
no sender was written. `ankerctl` itself can act as a local OctoPrint-compatible
upload target; if it is running, the `octoprint` method may work through it, but
that was not verified.

### 2026-11-26 · Tiertime and Afinia · UP Studio network printing · Not supported

Tiertime's UP printers, and Afinia's rebadged H+1 HS, connect over Wi-Fi and
Ethernet but only through Tiertime's own software (UP Studio and Tiertime Print
Queue). No public API or upload protocol exists; print-farm services describe it
as a closed, vendor-specific protocol
([Printago](https://printago.io/printers/tiertime-up300-hs),
[SimplyPrint](https://simplyprint.io/compatibility/tiertime-up300-hs)). Their
Orca presets use Tiertime-specific start G-code (`M970`/`M974`), and whether the
printers accept uploaded G-code at all is unconfirmed. No sender was written.
USB-stick printing remains the route outside UP Studio. Tiertime support could be
asked for an SDK.

### 2026-11-26 · Remaining preset brands · Coverage sweep · Mapped to existing protocols

Every remaining vendor in the preset catalog was classified by the resolved
`gcode_flavor` of its machine presets and its `host_type`:

- Klipper models without a vendor protocol (list under Caveats, including Cubicon,
  whose presets name `octoprint` but target Klipper) are offered `moonraker`.
- Marlin-only brands use `octoprint`: Folgertech, Geeetech, Lulzbot, Tronxy,
  Voxelab, Wanhao, WEMAKE3D, and the Marlin models of the brands above.
- `Custom` covers Klipper, Marlin, RepRapFirmware, and Repetier generics; Repetier
  Server has its own API, which is a separate candidate.
- `OrcaArena` is a test printer definition, not real hardware, and is skipped.

No sender was added in this entry, so it is part of a mapping commit rather than a
new type.

### 2026-11-26 · Flashforge (older models) · Legacy TCP console · Implemented

Older Flashforge printers speak FlashPrint's G-code console on TCP port 8899, with
`~`-prefixed commands answered by `ok`. The sender follows OrcaSlicer's
Flashforge serial path (read for protocol only):

1. `~M601 S1` (take control), `~M650` (legacy session), `~M119`, requiring
   `MachineStatus: READY`.
2. `~M28 <size> 0:/user/<name>`, then the G-code in raw 4 KiB writes.
3. After 3 seconds, `~M29` to save, then `~M23 0:/user/<name>` to print.

### 2026-11-26 · QIDI (older models) · QIDI UDP protocol · Implemented

QIDI's pre-Klipper printers take commands as UDP datagrams on port 3000. The
open-source [QidiPrint](https://github.com/alkaes/QidiPrint) Cura plugin (read for
protocol facts only; it is AGPL) shows the flow the sender follows:

1. `M4001` (connect), then `M4000`, requiring the idle flag in `D:…/…/1`.
2. `M28 <name>`, then blocks of up to 1280 bytes, each followed by its offset as
   4 little-endian bytes, an XOR checksum over data and offset, and `0x83`;
   `resend <n>` rewinds to byte `n`.
3. `M29 <name>`, then `M6030 ":<name>" I1` to print.

Unlike the plugin, a block that gets no reply after three tries fails the send
instead of being skipped.
