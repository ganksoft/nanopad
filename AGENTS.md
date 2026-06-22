# Agent Instructions

## Project Overview
Nanopad is a portable Win32 text editor written in C++ using the Win32 API directly — no frameworks, no MFC, no ATL.

## Code Style
- Use `.clang-format` in the repo root. Always run clang-format on modified source files after changes.
- Do not reorder `#includes` — Windows headers are order-dependent.
- Only comment non-obvious code. Do not comment the obvious.

## Build
- Visual Studio 2022 or later, v143+ toolset, x64 Debug/Release.
- Build from repo root: `msbuild Nanopad.sln /p:Configuration=Release /p:Platform=x64`
- Always verify a clean build after code changes.

## Architecture
- Source files live flat in `src/`, project files in `src/`, solution at root.
- All app settings stored in `nanopad.ini` next to the exe (portable, no registry for app settings).
- Version info lives in `src/version.h` — CI stamps it from git tags. Never manually bump the version.
- Dark mode uses undocumented Windows UAH messages (0x0091, 0x0092) and uxtheme ordinal APIs.

## Coding Practices
- Cache GDI objects (brushes, fonts, pens) — never create/destroy in paint handlers.
- Use `std::make_unique` for dynamic allocations, not raw `new`/`delete`.
- Prefer stack buffers for known-bounded sizes over `std::wstring`.
- All `LoadLibrary` calls must use full system paths to prevent DLL hijacking.
- All fixed-size buffers must have explicit bounds checks before writes.
- Keep the README updated when adding or changing features.
- Keep `CHANGELOG.md` updated for user-facing changes. Add new entries under `Unreleased`, and only move them into a versioned section when cutting a release.

## Testing
- No automated test suite — verify manually after changes.
- Test dark/light mode switching, DPI changes, file encoding round-trips, and large files.

## Releasing
- Releases are fully automated by `.github/workflows/release.yml`, triggered by pushing a `v*` tag (e.g. `v1.1.0`). Do not build or upload release artifacts by hand.
- Version numbers come from the tag, not the source. `src/version.h` stays at the `-dev` placeholder in the repo; CI rewrites `SN_VERSION_*` from the tag during the release build. Never manually bump `src/version.h`.
- Before tagging, move the relevant `CHANGELOG.md` entries from `## [Unreleased]` into a new `## [x.y.z] - YYYY-MM-DD` section and commit that on the default branch.
- Cut a release by tagging that commit and pushing the tag, e.g.:
  - `git tag v1.1.0`
  - `git push origin v1.1.0`
- Never push tags or trigger a release without explicit permission.
- On a tag push, CI: stamps `version.h`, builds Release for x64 and ARM64, packages each into `Nanopad-<ver>-<arch>.zip` (exe + README + CHANGELOG + LICENSE + `SHA256SUMS.txt`), creates the GitHub Release with auto-generated notes and SHA256 hashes, and publishes the new version to winget as `Ganksoft.Nanopad`.
- Use a `vMAJOR.MINOR.PATCH` tag (semantic versioning). The leading `v` is stripped for the package version.
- Winget publishing requires the `WINGET_TOKEN` secret; a missing or expired token fails only that final step, not the GitHub Release.
