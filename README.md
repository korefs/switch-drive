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
- Navigate the console interface in Portuguese with Joy-Con or Pro Controller.

### File support

| File type                               | Download |     Install | Current state                                                                            |
| --------------------------------------- | -------: | ----------: | ---------------------------------------------------------------------------------------- |
| `.nro`                                  |      Yes |         Yes | Standalone homebrew is supported                                                         |
| `.nsp`                                  |      Yes |         Yes | Installs one base game, update, or DLC through the Goldleaf-derived NCM adapter |
| `.xci`, `.nsz`, `.zip`, and other files |      Yes |          No | Stored as regular downloads                                                              |
| Native Google documents                 |       No |          No | Metadata can be listed; export is planned                                                |

### Controls

- **L/R:** change the main section.
- **A:** select or open.
- **B:** go back.
- **X:** download the selected Drive file.
- **Y:** download and install the selected Drive file.
- **Y in Library:** remove the selected managed NSP component; saves are retained.
- **L while browsing:** switch between My Drive and Shared with me.
- **+:** close the app.

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
- Add English and other translations, themes, accessibility settings, and
  configurable controls.

## Build the Switch client

Install devkitPro's `switch-dev`, `switch-curl`, `switch-mbedtls`, and
`switch-jansson` packages, then run:

```sh
make
```

Copy `switch-drive.nro` to `sd:/switch/switch-drive/switch-drive.nro` and run
it from hbmenu as an application (hold R while launching a game).

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
