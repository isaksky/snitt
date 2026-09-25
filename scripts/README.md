# Windows releases and Scoop

Build the Windows package with `bin/package-windows.ps1 -Version 0.1.0`.
It writes the ZIP and checksum file to `build/release`. Copy both to the Mac,
then generate the Scoop manifest from those exact bytes:

```sh
scripts/update-scoop-manifest.sh --write 0.1.0 build/release isaksky/xshot --private
scripts/update-scoop-manifest.sh --check 0.1.0 build/release isaksky/xshot --private
```

Omit `--private` for a publicly downloadable release. The repository argument
can name a separate repository used for releases and the Scoop bucket.

Upload these four files to the GitHub release:

- `build/release/xshot_0.1.0_windows_amd64.zip`
- `build/release/xshot_0.1.0_checksums.txt`
- `bucket/xshot.json` (asset name `xshot.json`)
- `platform/windows/Install-Scoop.ps1` (asset name `Install-Scoop.ps1`)

For a private release, each user authenticates GitHub CLI with their own
account. GitHub CLI downloads the assets using its existing credentials; the
wrapper never reads or copies a token. In PowerShell on Windows:

```powershell
gh auth login
gh release download --repo isaksky/xshot --pattern Install-Scoop.ps1
& .\Install-Scoop.ps1 -Repository isaksky/xshot
```

The wrapper validates the ZIP against both SHA-256 records, places it in
Scoop's download cache, and installs the original manifest through the local
`xshot-authenticated` bucket. Scoop verifies its hash again and installs
FFmpeg as a dependency. Qt is included in the ZIP. Run the wrapper again to
update to the latest release, or pass `-Version 0.1.0` for a specific release.
The local bucket is deliberately not a Git clone; ordinary `scoop update`
cannot authenticate private asset URLs, so use the wrapper for xshot updates.
Uninstall normally with `scoop uninstall xshot`.

To validate downloads on macOS without installing anything:

```sh
pwsh -NoProfile -File platform/windows/Install-Scoop.ps1 \
  -Repository isaksky/xshot -Version 0.1.0 \
  -DownloadOnly -DownloadDirectory build/release-download-check
```

For a public release, publish `bucket/xshot.json` in the bucket repository:

```powershell
scoop bucket add xshot https://github.com/isaksky/xshot.git
scoop install xshot/xshot
xshot --quit
scoop update xshot
```

Run `scripts/test-scoop-install.ps1 -Version 0.1.0` on the Windows desktop
session before publication. It validates installation, forced update, login
startup, bundled Qt, the FFmpeg dependency, and uninstall against the local
release ZIP. It refuses to replace an existing Scoop installation. Remove
the standalone `Install.cmd` installation before migrating to Scoop.
