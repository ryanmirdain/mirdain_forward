# Forward releases and update feeds

The release pipeline builds macOS arm64, macOS Intel, and Windows x64 packages,
signs each update payload with Ed25519, publishes immutable GitHub Release
assets, and asks Aperture to publish the XML feeds. Aperture's download buttons
read the same feeds as Forward's native updaters.

Apple Developer ID signing/notarization and Windows Authenticode signing are
still deferred. The current packages use ad-hoc macOS signatures and unsigned
Windows executables. Ed25519 update signatures are automated here; they do not
establish publisher trust with macOS Gatekeeper or Windows SmartScreen. Add
platform signing before generating the final packages/signatures when that
account/certificate setup is ready. Do not label these builds notarized or
publisher-signed.

## One-time setup, when ready

First commit/push the Forward changes and Aperture changes to their existing
`main` branches, and deploy Aperture. Its three empty XML feeds are valid and
show no downloads until the first release. Its `Sync Forward updates` workflow
must exist on the default branch before it can receive dispatch events.

In `ryanmirdain/mirdain_forward`, create a GitHub Actions environment named
`release`. Limit release access to trusted tags/manual releases as appropriate
for your repository. Normal builds and pull requests never receive private
signing keys.

From the Forward repository, prepare the local release tools:

```sh
python3 -m venv .release-venv
.release-venv/bin/python -m pip install -r tools/release-requirements.txt
.release-venv/bin/python tools/release_updates.py init-keys
```

On Windows use `.release-venv/Scripts/python.exe` instead. The command creates
separate macOS and Windows Ed25519 key pairs in ignored `.release-keys/`.
Private files contain base64-encoded raw 32-byte seeds, are created with mode
0600 on Unix, and are never printed. Securely back up the directory. Do not
regenerate it for each release: installed clients trust the existing public key.
The command refuses to replace existing key files.

After authenticating GitHub CLI with repository administration access, run:

```sh
.release-venv/bin/python tools/release_updates.py configure-github
```

This command uploads the private values through stdin to **environment secrets**
in `release`, then configures the public repository variables. It can be rerun
with the same saved keys. It does not generate or rotate keys.

| Kind | Name | Value |
| --- | --- | --- |
| Environment secret | `FORWARD_UPDATE_PRIVATE_KEY_MACOS` | Saved macOS seed, base64 encoded |
| Environment secret | `FORWARD_UPDATE_PRIVATE_KEY_WINDOWS` | Saved Windows seed, base64 encoded |
| Repository variable | `FORWARD_UPDATE_PUBLIC_KEY_MACOS` | macOS public verification key |
| Repository variable | `FORWARD_UPDATE_PUBLIC_KEY_WINDOWS` | Windows public verification key |
| Repository variable | `FORWARD_UPDATE_FEED_MACOS_ARM64` | `https://mirdainsystems.com/forward/updates/macos-arm64.xml` |
| Repository variable | `FORWARD_UPDATE_FEED_MACOS_X86_64` | `https://mirdainsystems.com/forward/updates/macos-x86_64.xml` |
| Repository variable | `FORWARD_UPDATE_FEED_WINDOWS_X64` | `https://mirdainsystems.com/forward/updates/windows-x64.xml` |

Also add `APERTURE_PUBLISH_TOKEN` to Forward's `release` environment. It must
be able to send `repository_dispatch` events to `hbaig2021/aperture`; that API
requires Contents write permission on the target repository. Use an appropriately
scoped PAT or GitHub App token, account for expiration, and never put it in the
website or release artifacts. The regular `GITHUB_TOKEN` is limited to its own
repository and cannot dispatch to Aperture. This credential only needs to target
Aperture, not Forward.

The Aperture workflow uses its own job token to commit only the three XML files
to `main`. Allow GitHub Actions Contents write in that repository. If branch
rules require a PR or block bot pushes, allow this publisher through those rules
before enabling it; the workflow does not create branches or bypass protection.
Vercel's existing GitHub integration then deploys the resulting main commit.

## Publish a release

1. Update `project(... VERSION ...)` in `CMakeLists.txt`.
2. Add `packaging/updates/notes/VERSION.md` with the actual release notes.
3. Commit and push the release source to `main`.
4. Push the matching version tag, for example `v0.1.0`, or run **Release Forward**
   manually with an existing tag. Tags must point to a commit contained in main.

`Release Forward` checks the version, notes, expected public feed URLs, public
repository visibility, and matching private/public keys before building. It
fails clearly when signing/publishing configuration is absent. It calls the
same reusable Build apps workflow used by regular CI. Release-only metadata
binds every artifact to its exact source commit, version, feed, and public key.

After all three builds succeed, the release job:

1. Checks the artifact metadata and signs the final DMGs and Windows setup EXE.
2. Generates one XML appcast per platform, including versions, notes, signatures,
   exact sizes, compatibility, and versioned GitHub download URLs.
3. Stages all packages, feeds, and `SHA256SUMS` in a draft GitHub Release.
4. Publishes the release only after uploads finish.
5. Dispatches Aperture's **Sync Forward updates** workflow with the release tag.

Aperture validates that all three feeds belong to that published stable release,
match the published installer names and sizes, and contain update signatures.
It preserves older feed entries, refuses version downgrades and replacement of
a published version, then commits the feeds together. The sync trusts the public
Forward release as its source; native Sparkle/WinSparkle cryptographically verify
the downloaded packages using the public keys embedded in the installed app.

Feed deployment is asynchronous. A successful dispatch means Aperture's job was
queued, not that Vercel has finished deploying it. Check the Aperture workflow
and Vercel deployment before announcing availability. Feed caching is at most
60 seconds at the CDN and revalidated by browsers.

## Recovery and compatibility

If the build/sign/upload fails, the previous website feeds remain unchanged.
Unpublished draft assets may be replaced on retry. Published releases are never
overwritten by this workflow. If feed synchronization fails after publication,
run **Sync Forward updates** in Aperture manually with the published tag; do not
rebuild and replace that version. Fix a mistaken published build by issuing a
higher version. A sync of an already imported release is harmless.

Use the Windows setup EXE as the update payload. Portable ZIPs remain available
in GitHub Releases for developers but deliberately do not enable in-app updates.
The installer preserves app data, reuses its installed directory, and protects
running copies with a mutex. During a transfer Windows installation is declined
until the user finishes sending and installs again; macOS defers the relaunch.

The configured macOS baseline is 12.0 (Qt 6.9); Windows is 10.0.17763. Keep
`PLATFORMS` in `tools/release_updates.py`, build deployment targets, and the
installer minimum version aligned if those requirements change. Keep bundle
IDs, installer AppId, and installation paths stable.

Existing users need one manual installation of the first updater-enabled
release. Builds without public-key configuration do not contact the feed server
and cannot receive configuration retroactively.

Manual XML templates remain here for reference. The automated pipeline generates
the feeds directly; do not publish templates containing `@...@` placeholders.
