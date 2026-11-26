# Unnecessary Boomkin Reviewer

A small Windows app (C++ / Win32, one ~1.5 MB `.exe`, nothing to install) that takes a
Warcraft Logs link and tells a Balance Druid what the logs' "Active %" hides:

- **Real GCD uptime**: time actually spent casting or on the GCD, not "active" time.
  Hardcasts count from begin-cast to finish, instants count one GCD. Off-GCD buttons
  (Incarnation, potions, trinkets, Barkskin) don't count.
- **Pauses**: every gap of 1.5s+ inside combat, with what you pressed next.
- **Pulls**: in Mythic+ the run is split into pulls (a gap of 10s+ between casts), so
  walking between packs is not counted as downtime.
- **Astral Power cap**: time at 100, the longest cap windows and what you pressed while
  capped, Astral Power wasted by source, dots / Incarnation pressed at cap, hardcasts
  started at 90+.
- **Comparison**: paste a second link (a top parse) to get every number side by side.
- **Timeline**: a hand-painted GDI chart with Astral Power, glowing cap windows, the GCD
  activity strip, pauses, pulls, moons for Incarnation and stars for Fury of Elune.
  Mouse wheel zooms, drag pans, double-click resets. It has barely any reason to exist.

English and Português (Brasil).

## First run

1. On Warcraft Logs, open your profile settings and create an **API client**
   (any name; the redirect URL is not used). You get a **client ID** and a **client secret**.
2. Start `UnnecessaryBoomkinReviewer.exe`, open **Settings**, paste both, save.
   They are stored encrypted in Windows Credential Manager, never in a file.
3. Paste a log link and press **Analyze** (or Enter).

For a personal build you can skip step 2: copy `src/win/default_creds.example.h` to
`src/win/default_creds.h` (gitignored), fill it in and rebuild. Values saved in Settings
still take priority. Anyone with that exe can extract the secret, so don't share it.

The **Demo** button runs two synthetic fights (a sloppy and a tidy Boomkin) without any
network, so you can look around before setting up the API client.

Windows SmartScreen will warn the first time because the exe is not code-signed:
*More info > Run anyway*.

### Links that work

- `https://www.warcraftlogs.com/reports/<code>?fight=19&type=damage-done&source=182`
- `https://br.warcraftlogs.com/reports/<code>?fight=last` (any language subdomain)
- `fight` missing or `last` = the last fight in the report.
- `source` missing = the app picks the Balance Druid; if there are several, choose one
  in the **Player** box and press Analyze again (data is cached, so it is instant).

Only **public** reports are supported for now.

## Files

Everything lives in `%LOCALAPPDATA%\UnnecessaryBoomkinReviewer` (**Open data folder**):

- `spells.json`: which spell IDs are fillers, dots, spenders, forms, on/off the GCD.
  Spells are matched by ID and then by name, so a patch that changes an ID usually
  still works; if not, edit this file (delete it to get the defaults back). Casts the
  app does not recognise are listed under "Casts not in spells.json".
- `settings.json`: language, fixed GCD (0 = estimate from the log), pull-split gap.
- `cache\`: raw API responses, so re-analysing a log costs no API points.

## How the numbers are computed

- **GCD**: estimated from the log, as the shortest gap between two instants pressed back
  to back within ±30s (clamped to 0.75–1.5s). You can force a value in Settings.
- **Canceled casts** (a begin-cast that never finishes) are reported separately and do
  not count as busy time.
- **Raid fights** use the whole fight as combat time. **Dungeons** use only the pulls.
- **Astral Power** comes from the `classResources` attached to casts and resource events
  (Warcraft Logs stores it in tenths; the app converts). The app detects whether resource
  events report the value before or after the gain; the Summary shows which one it used.

Known gaps: channeled spells (Convoke) count as one GCD; private reports need an OAuth
login flow that is not implemented.

## Building

Cross-compile from Linux/WSL (what `build.sh` does, also runs the tests):

```sh
sudo apt install g++-mingw-w64-x86-64
./build.sh        # -> build/UnnecessaryBoomkinReviewer.exe
```

Or on Windows with Visual Studio / CMake:

```sh
cmake -S . -B build
cmake --build build --config Release
```

Layout: `src/core` is portable C++17 (link parsing, API client, analysis, report text,
demo data) and is unit-tested on any OS (`tests/`). `src/win` is the Win32 layer:
WinHTTP, Credential Manager, the window and the GDI timeline.

Third party: [nlohmann/json](https://github.com/nlohmann/json) 3.11.3 (MIT), single header in `third_party/`.
