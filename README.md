# Switch Drive

Switch Drive is a GPL-3.0 Nintendo Switch homebrew for accessing files from
Google Drive on an Atmosphère console. It pairs with Google through a phone,
browses Drive folders, downloads files directly to the microSD card, and keeps
a local library of downloads made by the app.

Large-file support requires HOS 4.0.0 or later.

## Features

- Pair a Google account from a phone using a short-lived URL and code, without
  typing Google credentials on the Switch.
- Add more Google accounts. The most recently connected account becomes the
  active account in the current MVP.
- Browse **My Drive** and **Shared with me**, including nested folders and items
  exposed through shared drives.
- Download files directly from Google to
  `sd:/switch-drive/downloads/<task-id>/`; file data does not pass through the
  pairing service.
- Download files of 4 GiB or more as native HOS concatenated files, preserving
  one logical filename on the Switch while avoiding FAT32's per-file limit.
- Resume an interrupted download only after validating its Drive revision, ETag,
  HTTP range, expected size, and checksum metadata; invalid partial data can be
  restarted without appending a full response to it.

On a FAT32 card inspected outside HOS, a large download appears as the
filesystem's concatenated-file directory and its internal segments. Keep that
directory intact; Switch Drive and HOS access it through the original logical
filename.
- Validate the downloaded size and the Google Drive MD5 checksum when one is
  available.
- Choose between **download** and **download and install** for supported files.
- Install standalone NRO homebrew under `sd:/switch/<app-name>/` using a
  temporary file before replacing the final executable.
- Optionally remove the downloaded package after a successful installation.
- Keep a local library of downloads and check for externally deleted files only
  when the user opens the corresponding item.
- Use a self-hosted OAuth service with encrypted refresh tokens, expiring
  pairings, one-time claims, rate limiting, PostgreSQL storage, and HTTPS through
  Caddy.
- Use a native 1280×720 graphical interface with a navy/cyan theme, the Switch
  shared system font, controller navigation, and touch targets. The interface
  is available in English (US), Portuguese (Brazil), or Spanish; English (US)
  is the default for new and migrated installations.

### Languages

The Switch client stores an explicit UI-language preference in local state; it
does not infer the console language. Open **Settings** and press **Y** to cycle
through `English (US)` → `Português (Brasil)` → `Español`. The setting applies
immediately and is retained after relaunch.

### File support

| File type                               | Download |     Install | Current state                                                                            |
| --------------------------------------- | -------: | ----------: | ---------------------------------------------------------------------------------------- |
| `.nro`                                  |      Yes |         Yes | Standalone homebrew is supported                                                         |
| `.nsp`                                  |      Yes |         Yes | Installs one base game, update, or DLC through the Goldleaf-derived NCM adapter |
| `.xci`, `.nsz`, `.zip`, and other files |      Yes |          No | Stored as regular downloads                                                              |
| Native Google documents                 |       No |          No | Metadata can be listed; export is planned                                                |

### Controls

- **D-pad / either stick:** move the highlighted card or list selection. Hold
  a direction to repeat. Left from the first column enters the section menu;
  Up/Down selects a section, and Right or A returns to the cards.
- **L/R:** change the main section.
- **A:** activate the highlighted card, select, or open.
- **B:** go back; on the main screen, focus the section menu.
- **X:** download the selected Drive file.
- **Y:** download and install the selected Drive file.
- **Y in Library:** remove the selected managed NSP component; saves are retained.
- **Y in Settings:** change the UI language.
- **L while browsing:** switch between My Drive and Shared with me.
- **+:** close the app.
- **Touch:** select sections and action cards. Tap a file row to select it;
  tap the selected row again to open/check it. Swipe to scroll. The footer
  shows controller shortcuts for downloading, installing, and going back.

Launch from a title override (hold **R** while opening a game) for NSP actions.
When opened as an applet, Switch Drive keeps the UI and actions available but
shows a persistent warning; any unavailable NCM operation is reported in the
app instead of closing it.

## Status

This repository contains an early MVP: the Switch client, the OAuth pairing
service, Docker deployment files, and host tests for the local state model and
PFS0 parser. NRO and transactional NSP installation are implemented. NSP
installation requires an Atmosphère console launched in application mode. A real
console and a private Google Cloud OAuth client are required for final
acceptance testing.

## Roadmap

### Next priorities

1. **Complete NSP installation and removal:** done for one base game, update,
   or DLC per NSP, with managed component removal and interrupted-install
   recovery. Full package signature verification remains planned below.
2. **Show a QR code on the Switch:** keep the short URL and pairing code as a
   fallback, while allowing the user to scan and authenticate immediately from
   a phone.
3. **Add a complete account switcher:** display account name and avatar, switch
   accounts without reconnecting, remember a folder per account, and revoke an
   account from the console.
4. **Create a durable transfer queue:** pause, resume, retry, reorder, and cancel
   downloads; restore interrupted tasks after reopening the app; show speed,
   remaining time, and required disk space.
5. **Improve package identification:** read title ID, name, version, required
   firmware, content type, and installed version before installation. Warn about
   missing base games, incompatible updates, and duplicate content.
6. **Complete library management:** install an existing download, download it
   again, delete only the package, uninstall managed content, remove a broken
   shortcut, and reconcile content removed by another application.
### Drive and browsing improvements

- Search the current folder or every connected account.
- Add favorites, recent locations, download history, and quick access to the
  last opened folders.
- Provide a dedicated browser for Shared Drives in addition to Shared with me.
- Resolve Google Drive shortcuts, preserve resource keys, and detect shortcut
  loops.
- Cache folder listings and thumbnails for faster navigation, with an explicit
  refresh action.
- Filter and sort by file type, title, size, modification date, and installed
  state.
- Show game icons, covers, update relationships, and DLC belonging to the
  selected base game.

### Installation and storage improvements

- Install homebrew bundles containing assets and configuration files instead of
  supporting only a standalone NRO.
- Offer streaming installation to reduce temporary microSD usage, after the
  download-first workflow is proven reliable.
- Add a storage dashboard showing free space, queued downloads, installed
  content, removable packages, and pending cleanup.
- Verify package integrity and signatures before installation, with clear
  diagnostics for damaged or unsupported files.
- Add optional bandwidth limits and an automatic pause when the console enters
  sleep mode or the network changes.
- Allow the user to choose the cleanup rule: always keep, always remove after a
  successful install, or ask each time.

### Longer-term ideas

- Export user-owned installed content and upload it to Drive with resumable
  uploads and a separate write permission.
- Synchronize selected Drive folders for offline use while the app is open.
- Add WebDAV, S3-compatible storage, Dropbox, and local network sources through
  the same provider interface.
- Send a download or install request from a phone or web dashboard to a paired
  console, requiring confirmation on the Switch.
- Add safe application updates, release notes, and rollback to the previous NRO.
- Export a diagnostic report with private data removed to simplify bug reports.
- Add themes, accessibility settings, and configurable controls.

## Build the Switch client

Install devkitPro's `switch-dev`, `switch-curl`, `switch-mbedtls`,
`switch-jansson`, `switch-sdl2`, and `switch-sdl2_ttf`
packages, then run:

```sh
make
```

Copy `switch-drive.nro` to `sd:/switch/switch-drive/switch-drive.nro` and run
it from Sphaira or hbmenu. Use application mode (hold R while launching a
game) for NSP operations. The graphical browser does not require title override.

### Startup and Sphaira

Version 0.2.5 uses two video buffers. With only one, the compositor can retain
the displayed frame while the app waits for a free buffer to draw the next
one, stopping input polling as well. The old first frame displayed
`Connect a controller` before polling input, so that frozen message did not
establish a HID failure. The renderer now follows the double-buffer setup in
the [libnx graphics example](https://github.com/switchbrew/switch-examples/blob/master/graphics/simplegfx/source/main.c).

Input uses libnx's standard pad API for all eight controller slots and handheld
Joy-Cons, with both sticks, D-pad, A/B/X/Y and L/R navigation. A activates the
highlighted card. The main menu polls input and handles + before presenting a
frame. SDL joystick polling was removed: the Switch SDL backend uses the same
libnx pad API and reconfigures HID, so it was not an independent fallback.
The footer reports the sampled connection and focus state. The boot log records
input polls and video dequeue/queue stages for frames 1–3, 60 and 300, plus
connection/focus changes and +, to distinguish a render stall from missing input.

Version 0.2.1 fixes an NRO packaging error: setting `ROMFS` and `ICON` alone
did not pass them to `elf2nro`. The previous artifact had no `ASET` section,
so mandatory `romfsInit()` failed and sent the app into the terminal fallback.
The build now embeds the icon, NACP, and RomFS, and a missing optional logo
no longer prevents the graphical interface from opening. This follows the
[Switch application template](https://github.com/switchbrew/switch-examples/blob/master/templates/application/Makefile).

Sphaira may run under different homebrew launch modes; its name alone does
not identify the available memory or services. Switch Drive reads the mode
from libnx, uses smaller network buffers and a bounded text cache in applet
mode, and records startup stages in `sd:/switch-drive/boot.log`. Network
initialization failures are shown when an online action is attempted.

Direct Sphaira launch still needs hardware acceptance. If it fails, retain
`boot.log` immediately after that attempt, before launching through R (each
launch replaces the log). Include the Sphaira, Atmosphère, and firmware
versions. A missing log means execution did not reach the logged startup
stage, or the SD log could not be written; it does not establish the cause.

## Run the pairing service

```sh
cp server/.env.example server/.env
# set the Google OAuth and encryption values in server/.env
docker compose --env-file server/.env up --build
```

`PUBLIC_ORIGIN` must be a public HTTPS URL registered in Google Cloud Console as
`<PUBLIC_ORIGIN>/oauth/google/callback`. Add each beta tester in the OAuth
consent screen. Testing-mode Drive refresh tokens expire after seven days.

The Switch client obtains its service URL from `sd:/switch-drive/config.json`:

```json
{"service_url":"https://drive.example.com"}
```

## Run the pairing service on Cloudflare Workers

The optional [`worker/`](worker/README.md) deployment target runs the same
pairing API on a public HTTPS Worker. It requires a managed PostgreSQL database
through Cloudflare Hyperdrive; do not expose the Docker PostgreSQL service on a
home network to the Internet. After configuring a custom domain and registering
`https://<host>/oauth/google/callback` in Google Cloud, deploy it with Wrangler
and use that public HTTPS origin as `service_url`.

## Security and data handling

The client never stores Google refresh tokens. It stores only a console session
credential and account IDs in `sd:/switch-drive/state.json`. The server encrypts
refresh tokens using `TOKEN_ENCRYPTION_KEY` before writing them to PostgreSQL.
Do not commit `.env`, console state, logs, or Google OAuth credentials.

## Attribution

The planned NSP installation adapter follows the NCM installation approach from
[Goldleaf](https://github.com/XorTroll/Goldleaf), GPL-3.0. The pinned upstream
source is kept under `third_party/Goldleaf`, and the integration boundary is in
`switch/source/installer.cpp`. Goldleaf's license and notices must remain with
redistributed source and binaries that incorporate its code.

## NSP safety

Before writing content, Switch Drive identifies a base game, update, or DLC
from its CNMT, shows its title ID and version, and asks for microSD or internal
user storage. It blocks downgrades and treats an equal version as already
installed. Existing content stays in place until replacement metadata commits.
An `install-journal.json` is recovered on the next launch if the app is
interrupted. Managed removal removes only that component and never calls
save-data deletion APIs.

Install only trusted packages. NSP installation requires Atmosphère and any
appropriate FS patches; execute Switch Drive by holding R while launching a
game, not as a restricted applet.

## Tests

Run portable model/parser tests without devkitPro:

```sh
cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests
```
