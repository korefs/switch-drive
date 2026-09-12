# Contributor Guidance

## Repository layout

- `switch/` is the Nintendo Switch C++20 client. Public headers are under
  `switch/include/switchdrive/`; implementation is under `switch/source/`.
- `server/` is the pairing/OAuth service. Its protocols and API literals are not
  Switch UI text.
- `tests/` contains portable CMake/CTest coverage for the shared state, PFS0,
  downloader, installer parsing, and localization behavior.
- `README.md` documents user-facing behavior and hardware acceptance context.
- `third_party/Goldleaf/` is a pinned vendored dependency. Do not edit it;
  place focused integration code in Switch Drive sources instead.

## Working conventions

- Preserve existing user changes and avoid unrelated formatting churn.
- Use C++20 and the surrounding project's compact style. Keep platform-specific
  Switch code guarded by `__SWITCH__` so portable tests remain host-buildable.
- Verify client changes with:

  ```sh
  cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests
  make
  ```

## Localization

- Do not add new runtime Switch-client literals outside `switch/source/i18n.cpp`.
  Use typed `i18n::TextId` values and `i18n::tr(...)` at UI/error call sites.
- Update English (`en-US`), Portuguese (`pt-BR`), and Spanish (`es-ES`) catalog
  entries together. Codes are canonical BCP 47 values; invalid or missing values
  must fall back to `en-US`.
- Keep persisted values, JSON fields, URLs, HTTP headers, API queries, file
  names, and protocol tokens stable. Add a portable test whenever adding a key.

## State and installation safety

- State migrations must be backward-compatible and state writes must retain the
  existing temporary/backup/atomic-save behavior.
- NSP install and removal must preserve save data, journal mutations before NCM
  changes, avoid downgrade/removal mistakes, and delete content only after an
  orphan check. Do not add save-data deletion APIs.
