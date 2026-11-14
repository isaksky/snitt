cask "snitt" do
  version "0.2.5"
  sha256 "3d5b11644f32e02bde1158aba172e2ff4b1eb23f8ac40d2b856d310b850f7209"

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
