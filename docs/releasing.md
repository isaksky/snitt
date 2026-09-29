# Preparing public releases

This is the implementation plan for public Snitt releases, reviewed on
2026-09-28. The chosen license for Snitt's original code is MIT, and the intended
publisher is an individual. The publisher's country and signing provider still
need to be selected. No signing service or GitHub Actions workflow has been
configured by this preparation work.

## What exists

| Area | Current state | Work before a public binary release |
| --- | --- | --- |
| Source license | Root MIT license; upstream notices retained | Review third-party inventory and Git history before changing repository visibility |
| Windows build | `bin/build.ps1 -Test`, MSYS2 UCRT64/Qt 6 | Run on a clean CI runner with recorded dependency versions |
| Windows packaging | `bin/package-windows.ps1`, private LGPL FFmpeg build, ZIP, checksums, source and notices | Validate the release package and split staging from final archive creation |
| Windows signing | No signing step | Enroll a publisher and add Authenticode signing and verification |
| Scoop | Generator supports private and public releases | Update license metadata from `Unknown` and regenerate against the final signed ZIP |
| macOS | Local development signing and bundle validation | Developer ID, hardened runtime, notarization, and stapling |
| Automation | No `.github/workflows` directory | Separate unprivileged build/test jobs from trusted release signing and publishing |

The packaging scripts now include the root MIT license in future Windows and
macOS archives. Existing archives have not been rebuilt or re-licensed.

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

## Select Windows signing

Code signing uses a trusted certificate to bind a publisher identity to the
binary and detect changes after signing. GitHub Actions runs the build and calls
the signing provider; GitHub does not issue a Windows publisher certificate.

| Option | Fit for Snitt | Enrollment |
| --- | --- | --- |
| Microsoft Artifact Signing, formerly Trusted Signing | Recommended paid option if eligible; Microsoft lists a starting price of US$9.99/month | Azure subscription, identity validation, Public Trust certificate profile |
| SignPath Foundation | Free option for an approved open-source project | Apply after the public source and build process are ready; acceptance and timing are not guaranteed |

Microsoft currently supports individual Public Trust applicants in the US and
Canada. Confirm country eligibility before provisioning resources; use an Azure
billing account of type Individual with matching legal identity details. The
publisher name comes from verified identity, not an arbitrary product label.
See Microsoft's [setup guide](https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart),
[pricing and reputation guidance](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation),
and the [SignPath Foundation application](https://signpath.org/apply).

Signing does not guarantee that a new app avoids SmartScreen warnings. Microsoft
says reputation accumulates over time, and paying for an EV certificate no
longer provides an automatic SmartScreen bypass. A local/self-signed certificate
does not establish public publisher trust.
[Microsoft's explanation](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

For Microsoft Artifact Signing:

1. Create an Azure subscription and an Artifact Signing account; complete
   individual identity validation in the Azure portal.
2. Create a **Public Trust** certificate profile, not a test or Private Trust
   profile.
3. Create an Entra application/service principal and a federated credential for
   the GitHub release environment. Grant only the Certificate Profile Signer
   role at the narrowest appropriate scope.
4. Configure GitHub with the Azure client, tenant, and subscription IDs; signing
   endpoint; signing account name; and certificate profile name. The IDs are
   configuration values, not private signing keys.
5. Use `azure/login` with OpenID Connect, followed by
   `azure/artifact-signing-action`. This avoids a long-lived Azure client secret
   or exporting the Windows signing key into GitHub.
6. Enable SHA-256 signing and RFC 3161 timestamping. Timestamping is required for
   signatures to remain valid beyond the service's short certificate lifetime.

Use the official [signing action](https://github.com/Azure/artifact-signing-action)
and [OIDC setup](https://github.com/Azure/artifact-signing-action/blob/main/docs/OIDC.md)
when implementing the workflow. Pin the chosen action versions to full commit
SHAs. Limit the federated identity to `isaksky/snitt` and the release environment,
and restrict that environment to authorized release refs. Pull-request jobs must
not have signing access. See
[GitHub's workflow security guidance](https://docs.github.com/en/actions/reference/security/secure-use).

## Target release workflow

Trigger a release from a protected version tag such as `v0.1.0`, resolving it to
one commit. Initially produce a draft GitHub Release for final installation
checks; publish after those checks pass.

1. **Build and test.** On a GitHub-hosted Windows runner, install the selected
   MSYS2 UCRT64/Qt toolchain and run `bin/build.ps1 -Test`. Record dependency
   versions, source hashes, and build configuration. Retain or pin the dependency
   inputs so a moving MSYS2 repository is not the only record of the build.
2. **Stage.** Deploy the application, Qt/QML plugins, approved playback runtime,
   installer scripts, and license/source material into a clean package directory.
3. **Check the package.** Validate dependencies with development paths removed.
   Run interactive capture/recording and installation checks on an unlocked
   Windows desktop. Hosted CI test success alone does not prove screen capture,
   hotkeys, multiple displays, or login startup. The current
   `tests/windows-package-smoke.ps1` includes interactive checks and needs a
   suitable session; do not silently count skipped checks as passing.
4. **Sign and timestamp.** Sign `snitt.exe`, shipped helper binaries, and installer
   PowerShell scripts. Inventory bundled DLLs: preserve valid vendor signatures
   and handle unsigned runtime DLLs under the chosen provider's signing policy.
   A ZIP is a container; signing the executable inside it is what Windows checks.
   `.cmd` wrappers do not support embedded Authenticode signatures. A future
   MSI/EXE installer needs its own signature after its payload is finalized.
5. **Verify.** Fail the release on missing/invalid required signatures, the wrong
   publisher, or missing timestamps. Use Windows SDK SignTool, including
   `signtool verify /pa /all /v`, and verify the payload extracted from the final
   archive as well. See [SignTool](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool).
6. **Archive and hash.** Create the final ZIP only after signing. Compute SHA-256
   from those bytes, then generate and check the public Scoop manifest with
   `scripts/update-scoop-manifest.sh` without `--private`. Never rebuild or modify
   files after signing or reuse the old unsigned archive's checksum.
7. **Draft and publish.** Upload the final archives, checksums, Scoop manifest,
   bootstrap script, and source/build material to a draft release for that commit.
   Validate installation and update from the exact artifacts. Publish the
   release, then update the public bucket to point to the available assets.

`bin/package-windows.ps1` currently builds, stages, compresses, and hashes in one
invocation. Split those phases before inserting signing; calling it again after
signing would recreate the staging directory and discard the signatures. Add
signature verification as a required release gate, with no fallback to an
unsigned public release. Keep CI builds available without signing credentials.
If runtime DLLs are signed, preserve their build provenance hashes and record
the final signed hashes separately: Authenticode changes the DLL bytes. Package
checks must distinguish the validated build from its signed release copy.

Give only the signing job `id-token: write`, and only the publishing job
`contents: write`. Pass artifacts between jobs by the current run and commit,
not by an unqualified latest artifact. Public fork builds should use ephemeral
hosted runners, not a personal Windows VM with access to local credentials.

## macOS release path

The current local certificate is for development. Public direct downloads need
an Apple Developer ID Application identity, signing of nested code followed by
the app, hardened runtime with the necessary Qt/QML entitlements, secure
timestamps, notarization, and stapling before final archiving. Use a temporary CI
keychain and keep Apple signing credentials in the release environment. See
[Apple's Developer ID guidance](https://developer.apple.com/developer-id/) and
[notarization requirements](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution).

Setting `SNITT_SIGN_IDENTITY` alone is not the whole release implementation:
`bin/sign-macos` currently signs the outer app without a notarization flow.
Also adapt `bin/package-macos`'s byte-hash checks for the private runtime:
re-signing nested Mach-O files changes their bytes. Validate the staged runtime
before signing, then verify the final signed bundle without weakening provenance
checks. Update the generated installation text, which currently describes a
local development certificate, only once Developer ID and notarization succeed.

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
- Set release versions consistently in application metadata, package names, and
  the Scoop manifest; the macOS plist currently contains a fixed `0.1.0`.
- Update the Scoop generator's license metadata when preparing the new artifacts.
- Build a clean unsigned CI package first; then connect the selected signing
  account and exercise the signed draft release process.
- Change private installation instructions to public downloads when the release
  is actually available. Confirm both clean installation and upgrade from xshot.
