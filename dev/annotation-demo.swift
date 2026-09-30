// Native macOS demo driver. Capture is always a single, explicitly selected window.
import AppKit
import ApplicationServices
import ScreenCaptureKit
import AVFoundation
import UniformTypeIdentifiers

struct DemoError: Error, CustomStringConvertible {
    let description: String
    init(_ message: String) { description = message }
}
func require(_ condition: Bool, _ message: String) throws {
    if !condition { throw DemoError(message) }
}
func pause(_ seconds: Double) async {
    try? await Task.sleep(nanoseconds: UInt64(max(0, seconds) * 1_000_000_000))
}
func attribute(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
    var result: CFTypeRef?
    guard AXUIElementCopyAttributeValue(element, name as CFString, &result) == .success else { return nil }
    return result
}
func string(_ element: AXUIElement, _ name: String) -> String {
    let value = attribute(element, name)
    if let url = value as? URL { return url.absoluteString }
    return value as? String ?? ""
}
func children(_ element: AXUIElement) -> [AXUIElement] {
    attribute(element, kAXChildrenAttribute) as? [AXUIElement] ?? []
}
func descendants(_ element: AXUIElement, depth: Int = 0) -> [AXUIElement] {
    guard depth < 22 else { return [] }
    return [element] + children(element).flatMap { descendants($0, depth: depth + 1) }
}
func frame(_ element: AXUIElement) -> CGRect {
    guard let pv = attribute(element, kAXPositionAttribute), let sv = attribute(element, kAXSizeAttribute),
          CFGetTypeID(pv) == AXValueGetTypeID(), CFGetTypeID(sv) == AXValueGetTypeID() else { return .zero }
    var p = CGPoint.zero, s = CGSize.zero
    AXValueGetValue(pv as! AXValue, .cgPoint, &p)
    AXValueGetValue(sv as! AXValue, .cgSize, &s)
    return CGRect(origin: p, size: s)
}
func describe(_ element: AXUIElement) -> String {
    [kAXTitleAttribute, kAXValueAttribute, kAXDescriptionAttribute].map { string(element, $0) }.filter { !$0.isEmpty }.joined(separator: " | ")
}
func windows(_ app: NSRunningApplication) -> [AXUIElement] {
    attribute(AXUIElementCreateApplication(app.processIdentifier), kAXWindowsAttribute) as? [AXUIElement] ?? []
}
func findText(_ root: AXUIElement, _ text: String) throws -> CGRect {
    let candidates = descendants(root).filter { describe($0).contains(text) }.map(frame).filter { $0.width > 1 && $0.height > 1 }
    guard let result = candidates.min(by: { $0.width * $0.height < $1.width * $1.height }) else {
        throw DemoError("Cannot locate demo target: \(text)")
    }
    return result
}
func setFrame(_ window: AXUIElement, _ rect: CGRect) throws {
    var origin = rect.origin, size = rect.size
    try require(AXUIElementSetAttributeValue(window, kAXPositionAttribute as CFString, AXValueCreate(.cgPoint, &origin)!) == .success, "Cannot position demo window")
    try require(AXUIElementSetAttributeValue(window, kAXSizeAttribute as CFString, AXValueCreate(.cgSize, &size)!) == .success, "Cannot resize demo window")
}
func writePNG(_ image: CGImage, to url: URL) throws {
    guard let dest = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil) else { throw DemoError("Cannot write PNG") }
    CGImageDestinationAddImage(dest, image, nil)
    try require(CGImageDestinationFinalize(dest), "PNG write failed")
}

@MainActor final class Input {
    var app: NSRunningApplication
    var window: AXUIElement?
    var movementDuration = 0.2
    init(_ app: NSRunningApplication) { self.app = app }
    func focus() async throws {
        app.activate(options: [])
        if let window { AXUIElementPerformAction(window, kAXRaiseAction as CFString) }
        await pause(0.25)
        try check()
    }
    func check() throws {
        try require(NSWorkspace.shared.frontmostApplication?.processIdentifier == app.processIdentifier,
                    "Focus left the demo app; stopped without sending further input.")
        if let window {
            let current = attribute(AXUIElementCreateApplication(app.processIdentifier), kAXFocusedWindowAttribute)
            try require(current != nil && CFEqual(current, window), "Focus left the demo window; stopped.")
        }
    }
    func key(_ code: CGKeyCode, flags: CGEventFlags = []) async throws {
        try check()
        for down in [true, false] {
            let e = CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: down)!
            e.flags = flags
            e.post(tap: .cghidEventTap)
            await pause(0.025)
        }
    }
    func text(_ value: String, interval: Double = 0) async throws {
        for fragment in interval > 0 ? value.map(String.init) : [value] {
            try check()
            let units = Array(fragment.utf16)
            for down in [true, false] {
                let e = CGEvent(keyboardEventSource: nil, virtualKey: 0, keyDown: down)!
                e.flags = []
                e.keyboardSetUnicodeString(stringLength: units.count, unicodeString: units)
                e.post(tap: .cghidEventTap)
            }
            await pause(max(0.025, interval))
        }
    }
    func mouse(_ type: CGEventType, _ point: CGPoint) throws {
        try check()
        CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: point, mouseButton: .left)!.post(tap: .cghidEventTap)
    }
    func move(to point: CGPoint, duration: Double? = nil, dragging: Bool = false) async throws {
        let duration = duration ?? movementDuration
        let start = CGEvent(source: nil)!.location
        let steps = max(1, Int(duration * 60))
        let clock = ProcessInfo.processInfo.systemUptime
        for step in 1...steps {
            let t = Double(step) / Double(steps), eased = t * t * (3 - 2 * t)
            try mouse(dragging ? .leftMouseDragged : .mouseMoved,
                      CGPoint(x: start.x + (point.x - start.x) * eased, y: start.y + (point.y - start.y) * eased))
            await pause(clock + duration * t - ProcessInfo.processInfo.systemUptime)
        }
    }
    func drag(from: CGPoint, to: CGPoint, duration: Double) async throws {
        try await move(to: from)
        try mouse(.leftMouseDown, from)
        // Always release a held button, including on focus loss.
        defer { CGEvent(mouseEventSource: nil, mouseType: .leftMouseUp, mouseCursorPosition: CGEvent(source: nil)!.location, mouseButton: .left)!.post(tap: .cghidEventTap) }
        try await move(to: to, duration: duration, dragging: true)
    }
    func click(_ point: CGPoint) async throws {
        try await move(to: point)
        try mouse(.leftMouseDown, point)
        await pause(0.04)
        try mouse(.leftMouseUp, point)
    }
}

// No display/desktop capture API is used anywhere in this program.
func captureConfiguration(_ window: SCWindow, crop: CGRect? = nil, scale: Double = 1) -> SCStreamConfiguration {
    let config = SCStreamConfiguration()
    let bounds = crop ?? CGRect(origin: .zero, size: window.frame.size)
    config.sourceRect = bounds
    config.width = Int(bounds.width * scale) / 2 * 2
    config.height = Int(bounds.height * scale) / 2 * 2
    config.minimumFrameInterval = CMTime(value: 1, timescale: 60)
    config.queueDepth = 5
    config.showsCursor = true
    config.capturesAudio = false
    config.captureMicrophone = false
    config.ignoreShadowsSingleWindow = true
    config.includeChildWindows = false
    return config
}
func captureWindow(_ window: SCWindow, crop: CGRect? = nil) async throws -> CGImage {
    let config = captureConfiguration(window, crop: crop)
    config.showsCursor = false
    return try await SCScreenshotManager.captureImage(contentFilter: SCContentFilter(desktopIndependentWindow: window), configuration: config)
}
func shareableWindow(_ app: NSRunningApplication, ax: AXUIElement, title: String) async throws -> SCWindow {
    let content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: true)
    let bounds = frame(ax)
    let candidates = content.windows.filter {
        $0.owningApplication?.processID == app.processIdentifier && $0.title == title
        && abs($0.frame.minX - bounds.minX) < 3 && abs($0.frame.minY - bounds.minY) < 3
        && abs($0.frame.width - bounds.width) < 3 && abs($0.frame.height - bounds.height) < 3
    }
    try require(candidates.count == 1, "Cannot uniquely identify the allowlisted \(title) window; refusing capture.")
    return candidates[0]
}

@MainActor final class Recorder: NSObject, SCRecordingOutputDelegate, SCStreamDelegate {
    var stream: SCStream?
    var started = false, finished = false
    var failure: Error?
    nonisolated func recordingOutputDidStartRecording(_ recordingOutput: SCRecordingOutput) { Task { @MainActor in self.started = true } }
    nonisolated func recordingOutputDidFinishRecording(_ recordingOutput: SCRecordingOutput) { Task { @MainActor in self.finished = true } }
    nonisolated func recordingOutput(_ recordingOutput: SCRecordingOutput, didFailWithError error: Error) { Task { @MainActor in self.failure = error } }
    nonisolated func stream(_ stream: SCStream, didStopWithError error: Error) { Task { @MainActor in self.failure = error } }
    func start(_ window: SCWindow, crop: CGRect?, to url: URL) async throws {
        try require(!FileManager.default.fileExists(atPath: url.path), "Output already exists: \(url.lastPathComponent)")
        let config = SCRecordingOutputConfiguration()
        config.outputURL = url
        config.videoCodecType = .h264
        config.outputFileType = .mp4
        let output = SCRecordingOutput(configuration: config, delegate: self)
        let stream = SCStream(filter: SCContentFilter(desktopIndependentWindow: window),
                              configuration: captureConfiguration(window, crop: crop), delegate: self)
        self.stream = stream
        try stream.addRecordingOutput(output)
        try await stream.startCapture()
        for _ in 0..<200 {
            if let failure { throw failure }
            if started { return }
            await pause(0.05)
        }
        throw DemoError("Window recorder did not become ready")
    }
    func stop() async throws {
        guard let stream else { return }
        try await stream.stopCapture()
        for _ in 0..<200 {
            if let failure { throw failure }
            if finished { self.stream = nil; return }
            await pause(0.05)
        }
        throw DemoError("Window recorder did not finish writing")
    }
}

struct Action: Decodable {
    let tool: String
    let mode: String?
    let target: String
    let duration: Double?
    let text: String?
}
struct Timeline: Decodable {
    let duration: Double
    let movementDuration: Double
    let actions: [Action]
}
func process(_ executable: String, _ args: [String]) throws {
    let p = Process()
    p.executableURL = URL(fileURLWithPath: executable)
    p.arguments = args
    try p.run()
    p.waitUntilExit()
    try require(p.terminationStatus == 0, "Command failed: \(URL(fileURLWithPath: executable).lastPathComponent)")
}
func saveJSON(_ value: Any, _ url: URL) throws {
    try JSONSerialization.data(withJSONObject: value, options: [.prettyPrinted, .sortedKeys]).write(to: url)
}
func rectJSON(_ r: CGRect) -> [String: Double] { ["x": r.minX, "y": r.minY, "width": r.width, "height": r.height] }

// Locate the white screenshot in the real editor, independently of toolbar size,
// Retina backing scale, and the editor's automatic fit-to-window zoom.
func imageBounds(in image: CGImage, source: CGSize) throws -> CGRect {
    let w = image.width, h = image.height
    var pixels = [UInt8](repeating: 0, count: w * h * 4)
    let space = CGColorSpaceCreateDeviceRGB()
    guard let ctx = CGContext(data: &pixels, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                              space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { throw DemoError("Cannot read calibration image") }
    ctx.draw(image, in: CGRect(x: 0, y: 0, width: w, height: h))
    func white(_ x: Int, _ y: Int) -> Bool {
        let i = (y * w + x) * 4
        return pixels[i] > 247 && pixels[i + 1] > 247 && pixels[i + 2] > 247
    }
    var bestX = 0, bestWidth = 0
    for y in 60..<h - 40 {
        var start = 0
        for x in 0...w {
            if x < w && white(x, y) { continue }
            if x - start > bestWidth { bestX = start; bestWidth = x - start }
            start = x + 1
        }
    }
    try require(bestWidth > w / 2, "Cannot find screenshot bounds in Snitt")
    let rows = (60..<h - 35).filter { white(bestX + 2, $0) && white(bestX + bestWidth - 3, $0) }
    guard let top = rows.first, let bottom = rows.last else { throw DemoError("Cannot find screenshot edges") }
    let result = CGRect(x: bestX, y: top, width: bestWidth, height: bottom - top + 1)
    try require(abs(result.width / result.height - source.width / source.height) < 0.01,
                "Screenshot calibration failed aspect-ratio check: \(result), source \(source)")
    return result
}

@MainActor final class Session {
    let root: URL, output: URL
    var sourceBounds = CGRect.zero, targets: [String: CGRect] = [:]
    var safari: NSRunningApplication!, safariAX: AXUIElement!, safariSC: SCWindow!
    var snitt: NSRunningApplication!, snittAX: AXUIElement!, snittSC: SCWindow!
    var input: Input!, canvas = CGRect.zero
    var browserCrop = CGRect.zero
    init(root: URL, output: URL) { self.root = root; self.output = output }
    func prepareSafari() async throws {
        let page = root.appendingPathComponent("dev/index.html")
        if NSRunningApplication.runningApplications(withBundleIdentifier: "com.apple.Safari").isEmpty {
            try process("/usr/bin/open", ["-a", "Safari"])
            await pause(1)
        }
        guard let app = NSRunningApplication.runningApplications(withBundleIdentifier: "com.apple.Safari").first else { throw DemoError("Safari did not launch") }
        safari = app
        let driver = Input(app)
        await pause(0.1)
        // Reuse only this fixture. Otherwise make a dedicated window, never a tab
        // alongside personal tabs. Browser chrome is excluded from all captures.
        let fixtureWindows = windows(app).filter { win in
            descendants(win).contains { element in
                string(element, kAXRoleAttribute) == "AXWebArea" &&
                [page.absoluteString, root.appendingPathComponent("example-sites/index.html").absoluteString].contains(string(element, "AXURL"))
            }
        }
        try require(fixtureWindows.count <= 1, "More than one demo window is open; close the duplicate before recording.")
        safariAX = fixtureWindows.first
        if safariAX == nil {
            try await driver.focus()
            try await driver.key(45, flags: .maskCommand) // New window
            await pause(0.4)
            guard let focused = attribute(AXUIElementCreateApplication(app.processIdentifier), kAXFocusedWindowAttribute) else { throw DemoError("No Safari window") }
            safariAX = (focused as! AXUIElement)
        }
        driver.window = safariAX
        try await driver.focus()
        try setFrame(safariAX, CGRect(x: 90, y: 60, width: 1280, height: 960))
        await pause(0.35)
        try await driver.key(37, flags: .maskCommand) // Location
        await pause(0.15)
        try await driver.text(page.absoluteString, interval: 0.025)
        try await driver.key(36)
        var web: AXUIElement?
        for _ in 0..<100 {
            let elements = descendants(safariAX)
            // Safari may ask to confirm a newly moved local file. Confirm only
            // an open panel that contains this exact fixture URL.
            if let sheet = elements.first(where: { string($0, kAXRoleAttribute) == "AXSheet" }) {
                let sheetElements = descendants(sheet)
                if sheetElements.contains(where: { string($0, "AXURL") == page.absoluteString }),
                   let open = sheetElements.first(where: { string($0, kAXRoleAttribute) == "AXButton" && string($0, kAXTitleAttribute) == "Open" }) {
                    AXUIElementPerformAction(open, kAXPressAction as CFString)
                }
            }
            web = elements.first { string($0, kAXRoleAttribute) == "AXWebArea" && string($0, "AXURL") == page.absoluteString }
            if let web, (try? findText(web, "demo_FAKE_")) != nil { break }
            await pause(0.1)
        }
        guard web != nil else { throw DemoError("Safari is not displaying the local demo page") }
        // Navigating to an already open file can reuse Safari's existing DOM.
        // Reload explicitly so page edits made between takes are represented.
        try await driver.key(15, flags: .maskCommand)
        await pause(0.6)
        try await driver.key(29, flags: .maskCommand) // Actual size, Command-0.
        await pause(0.4)
        guard let web = descendants(safariAX).first(where: { string($0, kAXRoleAttribute) == "AXWebArea" && string($0, "AXURL") == page.absoluteString }) else {
            throw DemoError("Safari left the fixture during reload")
        }
        let webBounds = frame(web)
        let footer = try findText(web, "Sandbox workspace")
        sourceBounds = CGRect(x: webBounds.minX, y: webBounds.minY, width: webBounds.width,
                              height: ceil((footer.maxY - webBounds.minY + 20) / 2) * 2)
        try require(webBounds.contains(sourceBounds), "Demo page does not fit in Safari's visible content area")
        let labels = ["apiKey": "demo_FAKE_", "draft": "Internal draft", "signing": "Request signing enabled",
                      "endpoint": "http://legacy.example.com/hooks", "delivered": "Delivered successfully",
                      "retries": "Retries exhausted", "response": "Internal Server Error",
                      "badNote": "Bad annotation space", "announcement": "Workspace announcement"]
        for (name, label) in labels {
            let rect = try findText(web, label)
            try require(sourceBounds.contains(rect), "Target outside Safari capture: \(name)")
            targets[name] = rect.offsetBy(dx: -sourceBounds.minX, dy: -sourceBounds.minY)
        }
        let errorText = try findText(web, "Internal Server Error")
        if let code = descendants(web).first(where: { string($0, kAXValueAttribute) == "500" && abs(frame($0).midY - errorText.midY) < 5 }) {
            targets["response"] = frame(code).offsetBy(dx: -sourceBounds.minX, dy: -sourceBounds.minY)
        } else { throw DemoError("Cannot locate the 500 response next to its error description") }
        // Select the announcement group, not its smaller text label.
        if let group = descendants(web).first(where: { string($0, kAXRoleAttribute) == "AXGroup" && string($0, kAXDescriptionAttribute) == "Workspace announcement" }) {
            targets["announcement"] = frame(group).offsetBy(dx: -sourceBounds.minX, dy: -sourceBounds.minY)
        }
        safariSC = try await shareableWindow(app, ax: safariAX, title: "Snitt annotation demo — Relay")
        browserCrop = sourceBounds.offsetBy(dx: -safariSC.frame.minX, dy: -safariSC.frame.minY)
        let source = try await captureWindow(safariSC, crop: browserCrop)
        try writePNG(source, to: output.appendingPathComponent("source.png"))
        try require(source.width == Int(sourceBounds.width) && source.height == Int(sourceBounds.height), "Safari capture has unexpected dimensions: \(source.width) × \(source.height), expected \(sourceBounds)")
        try saveJSON(["source": rectJSON(sourceBounds), "targets": targets.mapValues(rectJSON), "capture": "desktopIndependentWindow; browser content only"], output.appendingPathComponent("targets.json"))
        print("Safari source: \(source.width) × \(source.height); all \(targets.count) targets verified")
    }
    func prepareEditor(replaceDemo: Bool) async throws {
        let executable = root.appendingPathComponent("build/Snitt.app/Contents/MacOS/snitt").path
        try require(FileManager.default.isExecutableFile(atPath: executable), "Build Snitt first with bin/build")
        if NSRunningApplication.runningApplications(withBundleIdentifier: "local.snitt").isEmpty {
            try process("/usr/bin/open", ["-n", root.appendingPathComponent("build/Snitt.app").path, "--args", "--show"])
            for _ in 0..<50 {
                if let running = NSRunningApplication.runningApplications(withBundleIdentifier: "local.snitt").first, !windows(running).isEmpty { break }
                await pause(0.1)
            }
        } else { try process(executable, ["--show"]) }
        await pause(0.6)
        guard let app = NSRunningApplication.runningApplications(withBundleIdentifier: "local.snitt").first,
              let ax = windows(app).first(where: { string($0, kAXTitleAttribute) == "Snitt" }) else { throw DemoError("Snitt editor did not open") }
        let hasImage = descendants(ax).contains { describe($0).contains("Preview ") && describe($0).contains("Image ") }
        try require(!hasImage || replaceDemo, "Snitt already has an image. Finish it first, or use --replace-demo only for a previous demo take.")
        snitt = app; snittAX = ax
        input = Input(app); input.window = ax
        try await input.focus()
        try setFrame(ax, CGRect(x: 70, y: 50, width: 1340, height: 1010))
        try process(executable, [output.appendingPathComponent("source.png").path])
        await pause(0.5)
        try await input.focus()
        snittSC = try await shareableWindow(app, ax: ax, title: "Snitt")
        let clean = try await captureWindow(snittSC)
        canvas = try imageBounds(in: clean, source: sourceBounds.size)
        try writePNG(clean, to: output.appendingPathComponent("calibration.png"))
        try saveJSON(["window": rectJSON(snittSC.frame), "image": rectJSON(canvas),
                      "safariWindowID": safariSC.windowID, "snittWindowID": snittSC.windowID,
                      "capture": "Only these exact windows; no display, desktop, child windows, or audio"], output.appendingPathComponent("capture.json"))
        print("Snitt image calibrated: \(canvas)")
    }
    func point(_ p: CGPoint) -> CGPoint {
        CGPoint(x: snittSC.frame.minX + canvas.minX + p.x * canvas.width / sourceBounds.width,
                y: snittSC.frame.minY + canvas.minY + p.y * canvas.height / sourceBounds.height)
    }
    func button(_ label: String) async throws {
        let rect = try findText(snittAX, label)
        try await input.click(CGPoint(x: rect.midX, y: rect.midY))
    }
    func perform(_ action: Action) async throws {
        if let mode = action.mode { try await button(mode == "good" ? "Good" : "Bad") }
        try await button(action.tool)
        guard let rect = targets[action.target] else { throw DemoError("Unknown timeline target: \(action.target)") }
        let expanded = rect.insetBy(dx: -6, dy: -5)
        switch action.tool {
        case "Text":
            try await input.click(point(CGPoint(x: rect.minX + 2, y: rect.minY + 5)))
            try await input.text(action.text ?? "", interval: 0.035)
            try await input.key(36, flags: .maskCommand)
        case "Arrow":
            let end = CGPoint(x: rect.maxX + 6, y: rect.midY)
            try await input.drag(from: point(CGPoint(x: end.x + 180, y: end.y - 28)), to: point(end), duration: action.duration ?? 0.3)
        case "Cut":
            // A vertical drag removes full-width rows. Preserve 18 px of spacing.
            let from = CGPoint(x: sourceBounds.width / 2, y: rect.minY - 9)
            let to = CGPoint(x: from.x, y: rect.maxY + 9)
            try await input.drag(from: point(from), to: point(to), duration: action.duration ?? 0.35)
        default:
            try await input.drag(from: point(expanded.origin), to: point(CGPoint(x: expanded.maxX, y: expanded.maxY)), duration: action.duration ?? 0.3)
        }
    }
}

@main struct Demo {
    @MainActor static func main() async {
        setbuf(stdout, nil)
        do { try await run() }
        catch { fputs("Demo stopped: \(error)\n", stderr); exit(1) }
    }
    @MainActor static func run() async throws {
        let args = CommandLine.arguments
        let mode = args.count > 1 ? args[1] : "preflight"
        print("Accessibility: \(AXIsProcessTrusted()); input events: \(CGPreflightPostEventAccess()); screen recording: \(CGPreflightScreenCaptureAccess())")
        try require(AXIsProcessTrusted() && CGPreflightPostEventAccess() && CGPreflightScreenCaptureAccess(),
                    "Enable Accessibility and Screen Recording for the invoking terminal/app in System Settings, then rerun.")
        if mode == "preflight" { print("Display: \(CGDisplayBounds(CGMainDisplayID()))"); return }
        if mode == "inspect" {
            for id in ["com.apple.Safari", "local.snitt"] {
                guard let app = NSRunningApplication.runningApplications(withBundleIdentifier: id).first else { continue }
                for window in windows(app) where ["Relay — Webhook overview", "Snitt annotation demo — Relay", "Snitt"].contains(string(window, kAXTitleAttribute)) {
                    print(id, "window", frame(window))
                    for element in descendants(window) {
                        let role = string(element, kAXRoleAttribute), desc = describe(element)
                        if !desc.isEmpty || role == "AXWebArea" { print(role, frame(element), String(desc.prefix(200)), string(element, "AXURL")) }
                    }
                }
            }
            return
        }
        try require(["prepare", "rehearse", "record"].contains(mode), "Unknown mode: \(mode)")
        try require(args.count >= 4, "Usage: driver prepare|rehearse|record REPOSITORY OUTPUT [--replace-demo]")
        let root = URL(fileURLWithPath: args[2]), output = URL(fileURLWithPath: args[3])
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        let session = Session(root: root, output: output)
        try await session.prepareSafari()
        let safariRecorder = Recorder()
        if mode == "record" {
            try await safariRecorder.start(session.safariSC, crop: session.browserCrop, to: output.appendingPathComponent("safari.mp4"))
            await pause(2)
            try await safariRecorder.stop()
        }
        try await session.prepareEditor(replaceDemo: args.contains("--replace-demo"))
        if mode == "prepare" { return }
        let timeline = try JSONDecoder().decode(Timeline.self, from: Data(contentsOf: root.appendingPathComponent("dev/timeline.json")))
        try require(timeline.duration == 26 && timeline.actions.last?.tool == "Cut", "The export expects a 26-second editor timeline with Cut last")
        try require(timeline.movementDuration.isFinite && timeline.movementDuration > 0
                    && timeline.actions.allSatisfy { $0.tool == "Text" || (($0.duration ?? 0).isFinite && ($0.duration ?? 0) > 0) },
                    "Movement and drag durations must be positive")
        session.input.movementDuration = timeline.movementDuration
        let recorder = Recorder()
        if mode == "record" { try await recorder.start(session.snittSC, crop: nil, to: output.appendingPathComponent("editor.mp4")) }
        do {
            let start = ProcessInfo.processInfo.systemUptime
            for (index, action) in timeline.actions.enumerated() {
                // Continue directly into the next tool movement. The pacing is
                // carried by cursor travel and drags, not gaps between actions.
                try session.input.check()
                let current = frame(session.snittAX), original = session.snittSC.frame
                try require(abs(current.minX - original.minX) < 1 && abs(current.minY - original.minY) < 1
                            && abs(current.width - original.width) < 1 && abs(current.height - original.height) < 1,
                            "Demo window moved or resized; stopped")
                print(String(format: "%05.2fs %@ → %@", ProcessInfo.processInfo.systemUptime - start, action.tool, action.target))
                try await session.perform(action)
                if mode == "rehearse" { try writePNG(try await captureWindow(session.snittSC), to: output.appendingPathComponent(String(format: "step-%02d.png", index + 1))) }
                if let failure = recorder.failure { throw failure }
            }
            let copy = try findText(session.snittAX, "Copy")
            try await session.input.move(to: CGPoint(x: copy.midX, y: copy.midY))
            try writePNG(try await captureWindow(session.snittSC), to: output.appendingPathComponent("finished-window.png"))
            let elapsed = ProcessInfo.processInfo.systemUptime - start
            try require(elapsed <= timeline.duration, "Actions exceed the editor duration; shorten movement/drag timings before export")
            print(String(format: "Actions complete at %.2fs; final result held until %.2fs", elapsed, timeline.duration))
            await pause(start + timeline.duration - ProcessInfo.processInfo.systemUptime)
            if mode == "record" { try await recorder.stop() }
            // Copy closes the editor. Capture has already stopped, so the desktop
            // revealed by closing a window can never enter the recording.
            try await session.button("Copy")
            await pause(0.2)
            guard let image = NSImage(pasteboard: .general), let tiff = image.tiffRepresentation,
                  let bitmap = NSBitmapImageRep(data: tiff), let cg = bitmap.cgImage else { throw DemoError("Snitt did not copy the finished screenshot") }
            let removed = (session.targets["announcement"]!.height + 18) * session.canvas.height / session.sourceBounds.height
            try require(cg.width == Int(session.sourceBounds.width) && abs(Double(cg.height) - (session.sourceBounds.height - removed)) <= 3,
                        "Copied image dimensions do not match the finished demo")
            try writePNG(cg, to: output.appendingPathComponent("annotated.png"))
            print("Finished: \(output.path)")
        } catch {
            if mode == "record" { try? await recorder.stop() }
            throw error
        }
    }
}
