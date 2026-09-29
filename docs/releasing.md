# Preparing public releases

This is the implementation plan for public Snitt releases, reviewed on
2026-09-28. The chosen license for Snitt's original code is MIT. Public code
signing is deferred by choice: the initial Windows releases will be unsigned
ZIPs. No signing account, certificate enrollment, or signing credentials are
needed for this plan. The workflows are defined in `.github/workflows`; they
become active when committed and pushed to GitHub.
The intended installation channels are Scoop on Windows and a Homebrew cask
on macOS, backed by versioned GitHub Release assets.

## What exists

| Area | Current state | Work before a public binary release |
| --- | --- | --- |
| Source license | Root MIT license; upstream notices retained | Review third-party inventory and Git history before changing repository visibility |
| Windows build | CI runs `bin/build.ps1 -Test` with MSYS2 UCRT64/Qt 6.11.2 | Confirm the first hosted run; package versions are recorded |
| Windows packaging | `bin/package-windows.ps1`, private LGPL FFmpeg build, ZIP, checksums, source and notices | Validate the exact release ZIP |
| Windows signing | Deferred; unsigned ZIPs are the chosen release path | Describe unsigned status in release notes |
| Scoop | New generated manifests use MIT; historical `Unknown` manifests remain readable | Test public install/update/uninstall, then merge the package update PR |
| macOS | Local development signing and bundle validation; arm64 ZIP | Public signing/notarization deferred; validate downloaded builds separately |
| Homebrew | Release automation generates an arm64 cask | Test locally, then merge the first package update PR to add `Casks/snitt.rb` |
| Automation | CI, shared platform builds, draft releases, and package update PRs | Push workflows and enable Actions to create pull requests |

The packaging scripts now include the root MIT license in future Windows and
macOS archives. Existing archives have not been rebuilt or re-licensed.

## Package-manager distribution

Keep the compiled ZIPs on GitHub Releases. Scoop and Homebrew each need a small
package definition that selects the version, download URL, and SHA-256; users
should not need a compiler or the Qt SDK. Start with an owned bucket and tap,
without making acceptance into the official package repositories a prerequisite.

For the simplest initial layout, the public `isaksky/snitt` repository can host
both the existing `bucket/snitt.json` and a new `Casks/snitt.rb`. Homebrew accepts
an explicit Git URL for a tap, so a separate `homebrew-*` repository is optional.
See [Scoop manifests](https://github.com/ScoopInstaller/Scoop/wiki/App-Manifests)
and [Homebrew taps](https://docs.brew.sh/How-to-Create-and-Maintain-a-Tap).

The intended commands **after the public assets and definitions are ready** are:

```powershell
scoop bucket add snitt https://github.com/isaksky/snitt.git
scoop install snitt/snitt
# Later, after finishing captures and quitting Snitt:
scoop update snitt
```

```sh
brew tap isaksky/snitt https://github.com/isaksky/snitt.git
brew install --cask isaksky/snitt/snitt
# Later, after finishing captures and quitting Snitt:
brew update
brew upgrade --cask isaksky/snitt/snitt
```

The fully qualified Homebrew install selects and trusts that cask on Homebrew
versions with explicit tap trust; it does not confer Apple publisher trust.
See [Homebrew tap trust](https://docs.brew.sh/Tap-Trust).

The existing Scoop definition installs `main/ffmpeg` and handles the shortcut,
login startup, and uninstall. Public downloads can use ordinary Scoop updates
without the private-release GitHub authentication wrapper. Generate the public
manifest only from the final public release bytes.

Use a Homebrew **cask** for the prebuilt GUI app. It should install `Snitt.app`
from `snitt_<version>_macos_arm64.zip`, declare Apple silicon and macOS 15+
support, and retain the shipped notices/source material. Qt and the macOS media
helpers are already bundled, so the current package needs no Homebrew Qt or
FFmpeg runtime dependency. Do not advertise Intel support until an Intel build
is produced and tested. See the [Cask Cookbook](https://docs.brew.sh/Cask-Cookbook).

A cask installing the app does not automatically reproduce `bin/install`'s
LaunchAgent setup. Initially document enabling login startup through macOS
Login Items, as the packaged app already does. Test migration from the existing
`~/Applications` install and remove its old login entry through the existing
uninstaller before introducing a second installation. Users still grant screen
recording permission through macOS; verify that permissions remain usable after
upgrades. Uninstall should remove package-owned integration without deleting
saved captures.

## Windows playback licensing

`bin/package-windows.ps1` reuses the MSYS2 Qt Multimedia FFmpeg plugin with
privately built LGPL-only `avcodec`, `avformat`, `avutil`, `swresample`, and
`swscale` DLLs. It excludes the codec DLLs from the
[MSYS2 package](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-ffmpeg)
identified as GPL-3.0-or-later. This is separate from the
`ffmpeg.exe` process installed through Scoop for recording and trimming.

MIT source can be combined with GPL components, but MIT does not remove the
combined distribution's GPL obligations. See the
[GNU explanation of combining licenses](https://www.gnu.org/licenses/gpl-faq.en.html#WhatDoesCompatMean).
Do not describe the Windows bundle as wholly MIT licensed; Qt and the private
FFmpeg runtime still retain their LGPL terms.

`bin/build-windows-multimedia-runtime.py` builds pinned FFmpeg source, checks the
actual DLL license/configuration exports and PE dependencies, and validates that
the existing Qt plugin uses the matching FFmpeg ABI. Packaging verifies the
copied DLL hashes and refuses to fill missing codecs from the SDK. This follows
the source/provenance approach used on macOS without requiring a Qt rebuild.
See [the Windows runtime instructions](windows-multimedia-runtime.md). Validate
playback, thumbnails, trimming, and recording on a clean Windows installation
when changing the pinned runtime.

Ship notices and the required corresponding source/build material for the exact
Qt and FFmpeg versions distributed, and support replacement of the LGPL shared
libraries. Keep the Lucide/Feather and omacut notices. These requirements remain
even with Snitt's MIT license. See
[FFmpeg's licensing guidance](https://ffmpeg.org/legal.html) and
[Qt's LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations).

## Unsigned Windows releases

GitHub Actions can build, test, package, and upload an unsigned Windows ZIP.
There is no signing-provider setup or signing job in the initial release plan.

Downloaded unsigned apps can trigger SmartScreen warnings. Enterprise policy or
Windows 11 Smart App Control can block execution, so do not promise that every
user can dismiss a warning. Explain the unsigned status in release notes and
provide SHA-256 checksums; checksums verify the downloaded bytes but do not
establish a trusted publisher identity. See
[Microsoft's reputation guidance](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

Signing can be reconsidered later if installation friction warrants it. The
[Microsoft signing action](https://github.com/Azure/artifact-signing-action) and
[SignPath Foundation](https://signpath.org/) are references for that future
choice, not prerequisites for opening the source or releasing a Windows ZIP.

## GitHub Actions

- `ci.yml` runs on pushes to `master`, pull requests, and manual dispatch. It
  runs fast Python metadata/policy checks plus the existing Qt headless test
  suites on Windows and Apple silicon macOS. It does not build the private media
  runtimes or create release archives.
- `build.yml` is the shared platform workflow. Release callers opt into building
  the private media runtimes and packaging. The Windows SDK is checked against
  Qt 6.11.2; macOS downloads the pinned official 6.11.2 SDK. Both jobs record
  their toolchain inventory and retain diagnostics for 14 days.
- `release.yml` runs for `v*` tags or a manual **Draft release** run naming an
  existing tag. It requires a stable `vX.Y.Z` tag whose commit is on the default
  branch's history, builds that exact commit, verifies both archives, generates
  package definitions, and creates a **draft** GitHub Release. It never publishes
  automatically. Reruns can refresh a draft with the same recorded source commit;
  published releases are never overwritten.
- `packages.yml` runs after a stable release is published, or through a manual
  **Update Scoop and Homebrew** run naming the latest published release. It
  requires a public repository, downloads both archives without authentication,
  checks hashes, embedded versions, and source provenance, and opens a PR updating
  `bucket/snitt.json` and `Casks/snitt.rb`. Older release retries cannot roll the
  package definitions back. The PR does not merge automatically.

### CI scope

CI runs only the headless suites and metadata checks. It does **not** run the
interactive package smoke script, real screen capture, hotkey/multiple-display
checks, install/upgrade/uninstall exercises, GPU tests, or benchmarks. Perform
those expensive checks locally against the exact draft assets before publishing.
Headless test logs show expected interactive-only skips; these are not evidence
that desktop behavior passed. Private FFmpeg/Qt runtime compilation is part of
creating release packages, not a PR test requirement.

The macOS job uses `SNITT_SIGN_IDENTITY=-` for ad-hoc bundle signatures. It needs
no signing certificate, signing secrets, keychain setup, or notarization account.
Normal local development installation retains its existing signing behavior.

MSYS2 is a rolling distribution. The workflow records installed package versions
and refuses an unexpected Qt/FFmpeg ABI rather than silently changing the release
runtime. If its repositories move beyond the supported SDK, deliberately update
and validate the runtime pins locally before using the newer SDK. The workflow
is not a fully reproducible snapshot of every transitive SDK package.

### First setup and release

1. Commit and push the workflows and supporting scripts. Under repository
   **Settings > Actions > General**, allow GitHub Actions to create pull requests
   so `packages.yml` can open the package update PR. The workflows request only
   the permissions each job needs and use the built-in `GITHUB_TOKEN`; no personal
   access token is required. Repository or organization policy may restrict this
   setting. Verify CI on the default branch before tagging.
2. Tag the chosen default-branch commit with a new stable version such as
   `v0.1.1` and push that tag, or manually run **Draft release** for an existing
   tag that contains these workflows. macOS bundle version fields are stamped
   in the disposable CI checkout; the tag's source tree is not rewritten.
3. Download the resulting draft assets and run the local release checks listed
   in `.github/release-notes.md`. Replace that checklist with the release notes
   and results. CI cannot certify the desktop/install tests on your behalf.
4. Publish the draft after validation. The package update workflow verifies that
   its asset URLs work anonymously and opens a PR. Test normal Scoop/Homebrew
   installation and upgrade from those URLs, then merge the PR to advertise the
   release through the bucket and tap. For a private repository, keep the draft
   private and defer the public package update until the repository is public.

A package update PR created with `GITHUB_TOKEN` can leave its CI runs waiting for
a maintainer to select **Approve workflows to run** in the PR. Its metadata and
Ruby syntax checks already run inside `packages.yml` before PR creation. Approve
the pending runs or use manual CI dispatch on its branch if needed; normal
protected-branch requirements still apply. If a release was
published by another workflow using `GITHUB_TOKEN`, manually dispatch the package
update workflow because token-created events do not normally start new runs.
See [GitHub's trigger rules](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow).

Both ZIPs, the combined SHA-256 list, generated `snitt.json` and `snitt.rb`, the
private-download bootstrap, SDK inventories, and `release.json` are uploaded to
the draft. `release.json` binds the two archive hashes and sizes to the source
commit. The metadata generator rejects mismatched versions, missing platforms,
duplicate checksums, and modified archives before generating package updates.
Corresponding source and license material remain inside the platform archives.

For local metadata verification without compiling the app:

```sh
python3 scripts/test-release-metadata.py
python3 scripts/test-scoop-manifest.py
python3 tests/windows_multimedia_runtime.py
```

Action revisions are pinned to full commit SHAs. Build jobs have read-only
repository access; draft publication has `contents: write`, and package updates
also have `pull-requests: write`. Checkout credentials are not persisted. Public
forks run on ephemeral hosted runners, never the personal Windows VM. See
[GitHub's workflow security guidance](https://docs.github.com/en/actions/reference/security/secure-use).

## Homebrew and macOS signing

Developer ID enrollment and notarization are also deferred. The existing local
development signing supports the current local install workflow and is not a
publicly trusted signing identity. Installing a cask does not make the app
notarized or exempt it from Gatekeeper. An unsigned or locally signed download
can still need the user's explicit approval to open. Test and document the
actual first-launch behavior; do not disable Gatekeeper or strip quarantine in
the cask. See [Apple's guidance](https://support.apple.com/en-us/102445).

The official `homebrew/cask` repository requires assessable macOS executables
to pass its Gatekeeper checks. The personal tap is the initial distribution
target while public signing is deferred; inclusion in the official repository
is a separate future decision. See
[Homebrew's cask requirements](https://docs.brew.sh/Acceptable-Casks).

## Remaining preparation

- Review the Git history for credentials, private material, and rights to all
  included code/assets before making the repository public. This preparation
  did not perform a full history or license audit.
- Repeat Windows package/desktop validation on the final release artifacts.
  The private LGPL runtime has passed playback, seeking, trimming, and recording
  controls on the Windows VM; clean installation, upgrade, global hotkeys, and
  physical GPU behavior still need release validation. One sparse-frame timing
  assertion failed on the VM and passed on repeat; investigate if it recurs.
- Add contributor/build instructions and a vulnerability reporting route.
- Validate the generated Homebrew cask locally, including install/upgrade/uninstall,
  first launch, login startup, and screen-recording permissions.
- Exercise the first hosted CI run, draft release, and bucket/tap update PR.
  Public signing enrollment remains deferred.
- Change private installation instructions to public downloads when the release
  is actually available. Confirm both clean installation and upgrade from xshot.
