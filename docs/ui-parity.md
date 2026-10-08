# Jolt UI parity audit

Reference: `4fd3106dcf28c9ced5411ff00f3f450764d6099d`. The immutable
`git archive` is in `build/ui-parity/reference-src`; its C++ application is
`build/ui-parity/reference-build/Snitt.app`. The port is built separately in
`build/jolt-snitt`. Qt 6.11.1 and Jolt 0.8.16 are used on macOS. No design
exceptions were approved. Original sources, icons, fonts and README are unchanged.

Reference and port use isolated settings/output directories and distinct IPC
identities (`snitt-ui-reference-4fd3106`, `snitt-ui-jolt-audit`, and final package
`snitt-ui-final-20261008` / `snitt-ui-verified-20261008`). Process command
lines were checked rather than assuming a launch created a new process. The
pre-existing production owner was left running. The initial Jolt audit wrapper
has a separate bundle ID for accessibility targeting. Final inspection uses the
staged production bundle and verifies its executable and loaded library paths.

## Discrepancies and verification

| ID | Reproduce | Expected / original source | Port regression and correction | Evidence |
|---|---|---|---|---|
| E1 | Open empty editor; resize | 1100×760, minimum 860×560; centered actions (`Main.qml`) | Restored dimensions, empty state and action placement | Empty wide/small fixtures; live desktop |
| E2 | Open image; resize; change tool/ink | 40px controls, original 26px colored icons, 12px hints; 168px ink segment; right finish group | Replaced generic flow/button layout with measured original groups, artwork and styles | Populated small/wide/large, hover/pressed/focus fixtures |
| E3 | Inspect canvas/footer | Full content canvas, 36px preview footer | Removed extra capture/status rows and canvas padding; preview help appears on press | Matched fixtures; native export and annotation suites |
| E4 | Type, wrap, scroll, resize, commit/cancel text | Transparent draft, native font inheritance, original cursors and resize grip; correct focus/shortcuts | Removed Material text insets and empty font override; restored draft frame, pointer states and focus policy | Draft/committed fixtures; 118 annotation checks; live text entry/Escape |
| A1 | Select one/many regions; resize selector; remove/arrange | Centered 960px maximum panel, original controls/mnemonics, badges/removal buttons, measured dimensions (`regionselector.cpp`) | Restored toolbar fitting, icons, labels, native tooltips, decoration and accessible checked modes; fixed repeated Video/Multi policy | Six selector fixtures including compact, drag/notice and fractional coordinates; 103 picker checks including native accessibility activation; live capture |
| A2 | Arrange multiple captures; reorder/change columns; annotate | Original phase labels, spacing, actions and confirmation (`Main.qml`) | Restored arrangement presentation and dialog placement; rejecting confirmation focuses canvas | Three arrangement fixtures, confirmation fixture, 47 arrangement checks |
| R1 | Prepare/start/stop/cancel recording | Cancel then mint Stop; 13px semibold labels; circular preparation wipe | Restored control size/order/style, status wording and rotating wipe | Four recording fixtures; native/Jolt control suites; real ScreenCaptureKit workflow |
| R2 | Review/play/seek/trim/export | Integrated 108px filmstrip, original handles/bubble, playback controls, time format, preview and error/progress rows (`TrimBar.qml`, `Main.qml`) | Replaced separate sliders; removed 112px consumed by hidden shortcut wrappers; corrected loaded/disabled state | Six review fixtures; five real-media playback/trim/export/cancel workflows |
| D1 | Open/cancel file dialog; confirm/reject; fail capture/reveal | Original wording, modality, transitions, canvas-relative placement and shortcut suppression | Restored Qt default/remembered file location and dialog styles; retained suppression through closing transition | Four dialog fixtures; app/capture/arrangement suites; native dialog inspection |
| D2 | Save then fail reveal; fail new capture | Save clears document before reveal error; capture error stays in its new editor | Corrected document clearing and session/error ownership; preserved other editors | App/capture regression checks; reveal/capture-error fixtures |
| S1 | Start with invalid settings or unavailable capture shortcut; reload settings | Usable defaults, original failure guidance, tray notices/menu/artwork (`appsettings.cpp`, `appservice.cpp`) | Restored startup fallback, delayed notices, camera tray icon/menu and live shortcut description | 76 settings checks, app suite, native Cocoa tray/menu test; manual OS popup limitation below |
| W1 | Multiple editors, close/cancel/capture; vary size/scale/screens | Original placement, focus, visibility, independent documents and capture numbering | Capture numbers no longer consume named-file session IDs; native placement retained | Session/IPC/placement suites; 1×/2× renders; actual one-display capture |
| W2 | Launch production bundle, grant screen capture, then choose Capture region from menu bar while another app is in front | Selector comes to the foreground and takes keyboard input | User saw no selector although the running packaged process owned a populated selector. Explicit macOS window activation now also activates the accessory application; Qt's requestActivate only makes a window key, and raise's app activation depends on native visibility | Inactive-accessory regression fails before/passes after; 135 Cocoa window checks, 103 region checks, desktop capture and 37-state command parity pass. User confirmed the rebuilt production menu-bar capture opens the image in the editor |

The reference's Back to Arrange button has contradictory parent/child visibility
conditions and cannot appear. The port's extra visible button was removed; its
command/confirmation remains tested. This is a source-established reference
behavior, not an approved redesign. Dialog timing, toolbar focus policies,
keyboard suppression, file-dialog defaults and native selector measurements were
also checked in source where a screenshot cannot establish behavior.

Application state, commands, settings policy, selection, document lifecycle,
recording and review remain Jolt-owned. New QML components and stateless native
artwork providers supply presentation and emit intent. Glimmer gained shared
button/layout/dialog/text controls, native tooltip presentation and per-window
snapshot support. Release scanning includes all shipped QML dependencies.

## Matched render evidence

`tests/ui_parity.py --output PATH --scale 1|2 [--desktop]` runs 35 identical states
against the immutable reference and Jolt. Evidence sets:

- `build/ui-parity/evidence`: offscreen 2×.
- `build/ui-parity/evidence-offscreen-1x`: offscreen 1×.
- `build/ui-parity/evidence-desktop`: actual macOS Cocoa 1× windows.

Each contains reference/Jolt PNGs, geometry, raw RGB differences for nonexact
renders, and `render-result.txt`. Every pixel is checked. There are no masks,
blurs, positional tolerances or edge envelopes. Qt compositing may differ by at
most 1/255 per channel; selectors permit 2/255 with exact alpha and independent
exact button geometry. Most editor/media/dialog states match exactly. Earlier
broader selector tolerance was removed after fixing duplicate display scaling,
fractional raster origins, clipping and native notice text painting.

Fixtures cover settled hover/press tooltips, keyboard focus, selected/disabled
controls, drafts, dialogs/errors, recording states and review/trim/export states.
The stopped indicator fixture uses the same rotation angle; live animation and
real media are checked separately. The fixtures supplement actual app workflows
and do not substitute for package inspection.

## Checks

`build/ui-parity/native-jolt.log`: full `bin/test` passed, including C++ editor
(36), placement (3), image tools (26), recording (21), Jolt UI (114), capture (41),
annotation (118), settings, picker, arrangement, gestures (95), document (558),
export (180 exact comparisons), trim jobs and `tests/parity.py` (37 command/pixel
states plus four retained cancellation guards). Platform/interactive-only native
cases are explicitly skipped in this offscreen run.

Affected Glimmer suites passed: layout 111, actions 32, pointer 28, text 29,
dialogs 23, slider/progress 29, tray 32. After later fixes, settings (76) and the
app/annotation/capture/recording suite were rerun successfully. Real-media review
passed all five workflows, duration/hash checks and cleanup on repeat. Real
ScreenCaptureKit recording passed ready/stop/save/review, startup cancellation,
320×200 H.264 probing and full decoding on repeat. Two initial media postconditions
failed intermittently; their original output is in the tool transcript and
`build/ui-parity/intermittent-checks.txt` records the failure and passing repeats.
Those initial runs are not counted as passes.
Final independent repeats also passed all recording and review postconditions
(`recording-final-repeat.log`, `review-final-repeat.log`).

The final selector check also exercises native accessibility actions, checked
values and repeated activation. The modes use exclusive Qt buttons and route
accessibility activation through the same click/Jolt command as pointer input.
All 103 picker checks pass (`jolt-region-accessibility.log`); live macOS inspection
confirmed checked values, disabled Multi in Video, repeated Video and capture
after switching back to Region. This check caught and corrected an intermediate
accessibility-only implementation that changed the checked flag without invoking
the Jolt command.

Windows evidence is under `build/ui-parity/windows/261008022538-81c4ff`: the
existing application and native suites passed (33 editor, 3 placement, 26 image,
20 recording; interactive/platform exclusions recorded). Glimmer and Snitt native
bridges also compiled on Qt 6.11.2/MinGW. No Windows migration was introduced.

Actual capture reports one display at scale 1.0. Physical high-DPI and
multi-monitor behavior are unavailable on this machine; simulated 2× rendering
and the multi-screen placement suite do not claim that hardware coverage.

The fresh 35-state comparison passed in all three evidence sets. The reference
and staged production app also repeated native file-dialog cancellation,
multiple-region removal/reselection, column changes/reordering, annotation,
multiline text with shortcut suppression, commit/Escape/focus restoration, actual
recording, playback, trim and save. The packaged 320×200 H.264 export was probed at
6.065 seconds after trimming a 7.58-second recording. Live inspection screenshots
and accessibility observations are retained in the task's tool transcript.

The native Cocoa tray test successfully opened the actual Qt menu and passed all
32 checks (`glimmer-tray-desktop.log`). Manual inspection of the OS tray popup
and notification presentation remains unverified: accessibility inspection timed
out with tray-only/menu states. Menu construction, icon painting, notification
wording and timings were compared against the unchanged reference source.

The initial verified production package was archived as
`build/ui-parity/Snitt-ui-parity-verified.zip`. `bin/stage-macos` completed signing
and dependency validation (`stage-accessibility-final.log`). The relocated
archive passed image export, direct/PATH launch, IPC forwarding, show, quit and
repeated teardown (`package-smoke-verified.log`). `verified-package.json` records
the inspected PID, exact packaged executable/library paths and SHA-256 hashes.
The rebuilt package's selector checked states, accessibility activation, repeated
Video selection and disabled Multi state were inspected again on macOS.
That exact package also repeated real 320×200 recording, playback, seeking,
trimming, save and editor restoration. Its final H.264 output was probed and
fully decoded (`verified-export.json`, `verified-live.log`).

The original final pass missed W2: accessibility and a per-window screenshot can
show a rendered selector even when the user cannot see it in the foreground.
The user's report reopened activation verification. Evidence for its correction is
under `build/ui-parity/activation/`: `before.log`, `after.log`, window/region/capture
logs, and `parity.log`. The new Cocoa regression creates a tool window while its
accessory app is inactive and checks AppKit activation, native visibility and Qt
keyboard focus. It exercises both activation APIs with Qt's incidental process
activation on raise enabled and disabled, so success does not depend on that
visibility-sensitive fallback. Additional application activation is macOS-only
and limited to explicit requests for focusable windows.

The correction was rebuilt with `bin/stage-macos`; the updated production app is
`build/package/macos/Snitt.app` and its archive is
`build/ui-parity/activation/Snitt-activation-fixed.zip`. Signing/dependency
validation passed (`activation/stage.log`), followed by relocated export,
direct/PATH launch, IPC forwarding, show, quit and repeated teardown
(`activation/package-smoke.log`). The previous production PID 76374 exited before
LaunchServices started PID 31216 with `--background`. `activation/package.json`
records the new executable, all three packaged bridge/platform libraries and
hashes. The user repeated Capture region from that app's menu bar, dragged a
rectangle, and confirmed that the captured image opened in the editor: "Yes,
capture works now." This closes W2 using direct user-visible verification.
The subsequent automated accessibility inspection timed out; that attempt is
not counted as a successful inspection. Hardware and other OS popup/notification
limitations recorded above remain unverified.
