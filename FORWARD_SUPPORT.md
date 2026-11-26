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
| `moonraker` | Voron, RatRig, Sovol, Qidi, Vzbot, FLSun, Kingroon, TwoTrees, Mellow, Vivedino, Positron3D, Artillery, ELEGOO Neptune 4, Custom Klipper | `src/printers/MoonrakerSender.cpp` | 2026-11-26 |
| `octoprint` | Lulzbot, Wanhao, Creality (Ender/CR), Anycubic, Geeetech and any other brand except Bambu Lab, through a USB-attached OctoPrint server | `src/printers/OctoPrintSender.cpp` | 2026-11-26 |
| `flashforge_lan` | Flashforge Adventurer 5M, 5M Pro, AD5X | `src/printers/FlashforgeSender.cpp` | 2026-11-26 |

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

Pending for: `moonraker`, `octoprint`, `flashforge_lan`.

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
    usable Moonraker without rooting; see Creality below.

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
