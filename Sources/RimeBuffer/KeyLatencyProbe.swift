import AppKit
import os

/// Always-on, aggregate-only timing for the macOS key path.
///
/// Records durations only: no key codes, text, client or schema identity.
/// Every `flushEvery` key-downs one summary line goes to `IMELog`, and the
/// same intervals are emitted as signposts (subsystem `org.scholay.rimes`,
/// category `KeyLatency`) for Instruments.
///
/// Phases (microseconds):
/// - `delivery`: event creation (`NSEvent.timestamp`) to `handle` entry, i.e.
///   the host app + InputMethodKit hop before any RIMES code runs.
/// - `handle` / `handle_up`: the whole `handle(_:client:)` callback for a
///   key-down / key-up. Summaries flush on key-down count only.
/// - `focus`: `adoptEventFocus` (nested inside `handle`).
/// - `engine`: every librime `processKey` call, chord paths included.
/// - `ui`: the whole `updateUI`. Its consecutive laps are:
///   `ui_prep` (lease and mode checks), `context` (`getStatus` +
///   `getContext`), `ui_state` (schema/status-menu bookkeeping), `marked`
///   (marked text sent to the host), `publish` (focus coordinator), `inline`
///   (Buffer inline composition), `caret` (caret rect asked of the host) and
///   `panel` (candidate window update or hide).
///
/// Not covered: the host applying marked text and WindowServer compositing
/// after the callback returns.
enum KeyLatencyProbe {
    enum Phase: Int, CaseIterable {
        case delivery, handle, handleUp, focus, engine, ui
        case uiPrep, context, uiState, marked, publish, inline, caret, panel
        /// Inside the candidate window: render, Buffer/clipboard caret query,
        /// panel layout, order-in, and the whole hide (which also runs on
        /// commit, outside `ui`).
        case panelRender, panelAnchor, panelLayout, panelShow, panelHide

        var label: String {
            switch self {
            case .panelRender: return "panel_render"
            case .panelAnchor: return "panel_anchor"
            case .panelLayout: return "panel_layout"
            case .panelShow: return "panel_show"
            case .panelHide: return "panel_hide"
            case .delivery: return "delivery"
            case .handle: return "handle"
            case .handleUp: return "handle_up"
            case .focus: return "focus"
            case .engine: return "engine"
            case .ui: return "ui"
            case .uiPrep: return "ui_prep"
            case .context: return "context"
            case .uiState: return "ui_state"
            case .marked: return "marked"
            case .publish: return "publish"
            case .inline: return "inline"
            case .caret: return "caret"
            case .panel: return "panel"
            }
        }
    }

    struct Span {
        fileprivate let start: CFAbsoluteTime
        fileprivate let signpost: OSSignpostIntervalState?
    }

    /// Consecutive segments of one call: each `mark` records the time since
    /// the previous mark (or creation) under the given phase.
    struct Lap {
        private var last = CFAbsoluteTimeGetCurrent()

        mutating func mark(_ phase: Phase) {
            let now = CFAbsoluteTimeGetCurrent()
            KeyLatencyProbe.record(phase, seconds: now - last)
            last = now
        }
    }

    struct Summary: Equatable {
        let count: Int
        let p50: Int
        let p95: Int
        let p99: Int
        let max: Int
    }

    static let flushEvery = 500

    private static let signposter = OSSignposter(
        subsystem: "org.scholay.rimes",
        category: "KeyLatency"
    )
    private static let lock = NSLock()
    private static var samples: [[Double]] = Array(
        repeating: [],
        count: Phase.allCases.count
    )

    /// Call at `handle` entry for key-down and key-up events; pair with `end`.
    static func begin(_ event: NSEvent) -> Span? {
        switch event.type {
        case .keyDown:
            let delivery = ProcessInfo.processInfo.systemUptime
                - event.timestamp
            // Synthetic events may carry a zero or stale timestamp.
            if delivery >= 0, delivery < 5 {
                record(.delivery, seconds: delivery)
            }
            return Span(
                start: CFAbsoluteTimeGetCurrent(),
                signpost: signposter.beginInterval("handle")
            )
        case .keyUp:
            return Span(start: CFAbsoluteTimeGetCurrent(), signpost: nil)
        default:
            return nil
        }
    }

    static func end(_ span: Span) {
        let elapsed = CFAbsoluteTimeGetCurrent() - span.start
        guard let signpost = span.signpost else {
            record(.handleUp, seconds: elapsed)
            return
        }
        signposter.endInterval("handle", signpost)
        record(.handle, seconds: elapsed)
        flushIfDue()
    }

    static func note(_ phase: Phase, since start: CFAbsoluteTime) {
        record(phase, seconds: CFAbsoluteTimeGetCurrent() - start)
    }

    static func summarize(_ microseconds: [Double]) -> Summary? {
        guard !microseconds.isEmpty else { return nil }
        let sorted = microseconds.sorted()
        func percentile(_ fraction: Double) -> Int {
            let index = Int((Double(sorted.count) * fraction).rounded(.down))
            return Int(sorted[min(sorted.count - 1, index)])
        }
        return Summary(
            count: sorted.count,
            p50: percentile(0.50),
            p95: percentile(0.95),
            p99: percentile(0.99),
            max: Int(sorted[sorted.count - 1])
        )
    }

    fileprivate static func record(_ phase: Phase, seconds: Double) {
        lock.lock()
        samples[phase.rawValue].append(seconds * 1_000_000)
        lock.unlock()
    }

    private static func flushIfDue() {
        lock.lock()
        guard samples[Phase.handle.rawValue].count >= flushEvery else {
            lock.unlock()
            return
        }
        let window = samples
        for index in samples.indices {
            samples[index].removeAll(keepingCapacity: true)
        }
        lock.unlock()

        let parts = Phase.allCases.compactMap { phase -> String? in
            guard let summary = summarize(window[phase.rawValue]) else {
                return nil
            }
            return "\(phase.label)_us n=\(summary.count) p50=\(summary.p50) "
                + "p95=\(summary.p95) p99=\(summary.p99) max=\(summary.max)"
        }
        IMELog.write("key latency " + parts.joined(separator: " | "))
    }
}
