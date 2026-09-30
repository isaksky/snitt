cask "snitt" do
  version "0.2.1"
  sha256 "082e2de44a507d7cb232d3aef55269975463e112aeafa5f7f9fe1a6ed1590121"

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
    Snitt is not notarized; macOS may require approval before its first launch.
    Grant screen-recording permission when prompted. To start at login, add
    Snitt in System Settings > General > Login Items.
    Finish captures and quit Snitt before upgrading.
  EOS
end
