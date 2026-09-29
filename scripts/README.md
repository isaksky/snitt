# Windows releases and Scoop

Before publishing the first Snitt release, rename the GitHub repository to
`isaksky/snitt` and rebuild the packages with the commands below. Snitt releases
use `snitt.json`, `snitt_<version>_windows_amd64.zip`, and
`snitt_<version>_checksums.txt`; a repository rename does not rename existing
release assets or the executables inside their archives.

Build the Windows package with `bin/package-windows.ps1 -Version 0.1.0`.
It writes the ZIP and checksum file to `build/release`. Copy both to the Mac,
then generate the Scoop manifest from those exact bytes:

```sh
scripts/update-scoop-manifest.sh --write 0.1.0 build/release isaksky/snitt --private
scripts/update-scoop-manifest.sh --check 0.1.0 build/release isaksky/snitt --private
```

Omit `--private` for a publicly downloadable release. The repository argument
can name a separate repository used for releases and the Scoop bucket.

Upload these four files to the GitHub release:

- `build/release/snitt_0.1.0_windows_amd64.zip`
- `build/release/snitt_0.1.0_checksums.txt`
- `bucket/snitt.json` (asset name `snitt.json`)
- `platform/windows/Install-Scoop.ps1` (asset name `Install-Scoop.ps1`)

For a private release, each user authenticates GitHub CLI with their own
account. GitHub CLI downloads the assets using its existing credentials; the
wrapper never reads or copies a token. In PowerShell on Windows:

```powershell
gh auth login
gh release download --repo isaksky/snitt --pattern Install-Scoop.ps1
& .\Install-Scoop.ps1 -Repository isaksky/snitt
```

The wrapper validates the ZIP against both SHA-256 records, places it in
Scoop's download cache, and installs the original manifest through the local
`snitt-authenticated` bucket. Scoop verifies its hash again and installs
FFmpeg as a dependency. Qt is included in the ZIP. Run the wrapper again to
update to the latest release, or pass `-Version 0.1.0` for a specific release.
The local bucket is deliberately not a Git clone; ordinary `scoop update`
cannot authenticate private asset URLs, so use the wrapper for Snitt updates.
Uninstall normally with `scoop uninstall snitt`.

To validate downloads on macOS without installing anything:

```sh
pwsh -NoProfile -File platform/windows/Install-Scoop.ps1 \
  -Repository isaksky/snitt -Version 0.1.0 \
  -DownloadOnly -DownloadDirectory build/release-download-check
```

For a public release, publish `bucket/snitt.json` in the bucket repository:

```powershell
scoop bucket add snitt https://github.com/isaksky/snitt.git
scoop install snitt/snitt
snitt --quit
scoop update snitt
```

Run `scripts/test-scoop-install.ps1 -Version 0.1.0` on the Windows desktop
session before publication. It validates installation, forced update, login
startup, bundled Qt, the FFmpeg dependency, and uninstall against the local
release ZIP. It refuses to replace an existing Scoop installation. Remove
the standalone `Install.cmd` installation before migrating to Scoop.
