cask "snitt" do
  version "0.2.6"
  sha256 "1d52b5a803b6c6b45d40aeb94435c9e118ce41292e550507268f7c395014df42"

  url "https://github.com/isaksky/snitt/releases/download/v#{version}/snitt_#{version}_macos_arm64.zip"
  name "Snitt"
  desc "Capture, annotate, combine, and record screen regions"
  homepage "https://github.com/isaksky/snitt"

  depends_on arch: :arm64
  depends_on macos: :sequoia

  app "Snitt.app"
  binary "#{appdir}/Snitt.app/Contents/MacOS/snitt"

  uninstall quit: "local.snitt"

  caveats <<~EOS
    Snitt is not notarized. Try opening Snitt from Applications first. If macOS
    blocks it because the developer cannot be verified, go to System Settings >
    Privacy & Security > Open Anyway, then confirm.

    For a copy you trust from Snitt's official release or Homebrew tap, you can
    alternatively remove its quarantine attribute and open it in Terminal:
      xattr -dr com.apple.quarantine "#{appdir}/Snitt.app"
      open "#{appdir}/Snitt.app"
    These commands apply only to Snitt's app bundle. They do not notarize it
    or disable Gatekeeper globally.

    Grant screen-recording permission when prompted. To start at login, add
    Snitt in System Settings > General > Login Items.
    Finish captures and quit Snitt before upgrading.
  EOS
end
