# Switch Drive

Switch Drive is a GPL-3.0 Nintendo Switch homebrew that lets its owner connect
Google Drive accounts, browse My Drive and Shared with me, download files, and
install local `.nro` and `.nsp` packages on an Atmosphere console.

## Status

This repository contains the MVP implementation: the Switch client, the OAuth
pairing service, Docker deployment files, and host tests for the durable model
and PFS0 parser. A real console and a private Google Cloud OAuth client are
required for final acceptance testing.

## Build the Switch client

Install devkitPro's `switch-dev`, `switch-curl`, and `switch-mbedtls` packages.
Clone Borealis in `external/borealis` at revision `1f7a05a` (or set
`BOREALIS_PATH` to an equivalent checked-out revision), then run:

```sh
make
```

Copy `switch-drive.nro` to `sd:/switch/switch-drive/switch-drive.nro` and run
it from hbmenu as an application (hold R while launching a game).

## Run the pairing service

```sh
cp server/.env.example server/.env
# set the Google OAuth and encryption values in server/.env
docker compose up --build
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

NSP installation is built around the installation approach from
[Goldleaf](https://github.com/XorTroll/Goldleaf), GPL-3.0. The integration is
kept in `switch/source/installer.cpp`; its upstream notices must remain with
redistributed binaries. The intended UI dependency is
[Borealis](https://github.com/natinusala/borealis), Apache-2.0.

## Tests

Run portable model/parser tests without devkitPro:

```sh
cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests
```
