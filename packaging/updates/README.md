# Signed Forward installer releases

Push a version tag such as `v1.1.0` after pushing the matching source commit to
main. **Release Forward** builds macOS arm64, macOS Intel, and Windows x64
installers, signs each finished package with Ed25519, and publishes it to a
public GitHub Release. There is no automatic XML generation, website commit,
or Aperture workflow dispatch.

## One-time signing configuration

Keep these secrets in Forward's GitHub Actions environment named `release`:

- `FORWARD_UPDATE_PRIVATE_KEY_MACOS`
- `FORWARD_UPDATE_PRIVATE_KEY_WINDOWS`

Keep these public repository variables:

- `FORWARD_UPDATE_PUBLIC_KEY_MACOS`
- `FORWARD_UPDATE_PUBLIC_KEY_WINDOWS`
- `FORWARD_UPDATE_FEED_MACOS_ARM64`: `https://mirdainsystems.com/forward/updates/macos-arm64.xml`
- `FORWARD_UPDATE_FEED_MACOS_X86_64`: `https://mirdainsystems.com/forward/updates/macos-x86_64.xml`
- `FORWARD_UPDATE_FEED_WINDOWS_X64`: `https://mirdainsystems.com/forward/updates/windows-x64.xml`

Feed URLs are embedded into the desktop apps so they can check the XML that
we maintain on the website. They do not give the release job access to Aperture.
`APERTURE_PUBLISH_TOKEN` is no longer used or required.

Existing configured keys need no changes. For a new installation of the release
system, create the `release` environment, authenticate GitHub CLI, then run:

```sh
python3 -m venv .release-venv
.release-venv/bin/python -m pip install -r tools/release-requirements.txt
.release-venv/bin/python tools/release_updates.py init-keys
.release-venv/bin/python tools/release_updates.py configure-github
```

On Windows use `.release-venv/Scripts/python.exe`. The operator-only `init-keys`
command creates separate key pairs under ignored `.release-keys/`; it refuses
to overwrite existing files. Securely back up that directory. Private seeds
are base64-encoded raw 32-byte values and are created with mode 0600 on Unix.
`configure-github` uploads private values through stdin to environment secrets
and configures the public keys and feed URLs. It does not rotate keys.

## Publish a version

1. Update `project(... VERSION ...)` in `CMakeLists.txt`.
2. Optionally write `packaging/updates/notes/VERSION.md` for the GitHub Release
   description. Otherwise GitHub generates release notes.
3. Commit and push main, then push its matching tag, such as `v1.1.0`.

Tags must have three numeric version components and match CMake's project
version. All component widths are accepted, including `v1.12.3`. The tag must
point to a commit contained in main. A normal main push only builds temporary
Actions artifacts; a version-tag push publishes the signed permanent downloads.
An existing tag can also be selected with **Actions > Release Forward > Run
workflow**. A retry from main uses the corrected workflow/tooling while retaining
the source commit identified by the original tag.

The workflow validates signing keys and build configuration, builds all three
platforms, verifies each artifact's source/version/public-key metadata, and signs
the final installers. It uploads everything to a draft GitHub Release and only
publishes the release after all uploads succeed. Published releases are never
replaced by this workflow; fix an already published build with a higher version.
Failed unpublished draft uploads may be retried.

## Permanent downloads and manual XML

The release contains:

- `mirdain_forward-macos-arm64.dmg`
- `mirdain_forward-macos-x86_64.dmg`
- `mirdain_forward-windows-x64-setup.exe`
- `signatures.json`
- `SHA256SUMS`

For example, the Apple Silicon v1.1.0 installer has this permanent versioned URL:

```text
https://github.com/ryanmirdain/mirdain_forward/releases/download/v1.1.0/mirdain_forward-macos-arm64.dmg
```

`signatures.json` records the tag, version, source commit, and each installer's
`url`, `length`, `edSignature`, `sha256`, platform, and `minimumSystemVersion`.
The signatures are detached Ed25519 signatures over the exact installer bytes.
Use those values when we manually write the website XML:

| JSON field | XML field |
| --- | --- |
| top-level `version` | enclosure `sparkle:version` and `sparkle:shortVersionString` |
| package `url` | enclosure `url` |
| package `length` | enclosure `length` |
| package `edSignature` | enclosure `sparkle:edSignature` |
| package `os` | enclosure `sparkle:os` |
| package `minimumSystemVersion` | item `sparkle:minimumSystemVersion` |

Write the release notes into each XML item's description or link them to a
release-notes page. For Windows also use
`sparkle:installerArguments="/SILENT /SP- /NOICONS"`. Templates in this directory
are reference examples; replace all placeholders before publishing them.
The XMLs live under `public/forward/updates/` on Aperture's deployed
`codex/main-site-revamp` branch. Update and deploy that site separately after
the installers are published. Until the XML is updated, clients continue seeing
the previous release. No cross-repository credentials are involved in releasing
Forward anymore.

## Platform signing and compatibility

This pipeline signs update packages for Sparkle/WinSparkle verification. Apple
Developer ID signing/notarization and Windows Authenticode publisher signing
remain deferred: the build still uses ad-hoc macOS signatures and unsigned
Windows executables. Update-package signatures do not remove OS publisher
warnings. Add OS signing before packaging and Ed25519 signing when that setup
is ready; never change installer bytes after calculating an update signature.

Release installers target macOS 12.0 and Windows 10.0.17763. Keep the deployment
targets, installer minimum, and `PLATFORMS` in `tools/release_updates.py` aligned
when requirements change. Existing clients need one manual installation of an
updater-enabled release. Windows portable ZIPs remain regular CI artifacts,
not published updater payloads; use the setup installer for updates.
