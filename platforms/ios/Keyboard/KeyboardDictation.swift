import AVFoundation
#if KEYBOARD_LAYOUT_TESTS
@testable import RIMES
#endif
import Speech
import UIKit
import WebKit

/// Experimental in-keyboard dictation without opening the app.
///
/// Keyboard extensions cannot open the microphone themselves. A hidden WKWebView can: WebKit
/// captures `getUserMedia` audio in its own processes and hands 16 kHz mono PCM back here,
/// where Apple speech recognition turns it into text. This is not an official API path; when
/// capture fails, the keyboard says so and nothing else changes.
@MainActor final class KeyboardDictation: NSObject {
    enum State: Equatable { case idle, starting, listening }
    private(set) var state: State = .idle { didSet { if state != oldValue { onState?(state) } } }
    /// A recognized word (or punctuation) and what else it may have been.
    struct Word: Equatable, Sendable { let text: String; let alternatives: [String] }
    /// Best words so far for this utterance, replacing the previous call's; `isFinal` once recognition ends.
    var onWords: ((_ words: [Word], _ isFinal: Bool) -> Void)?
    var onState: ((State) -> Void)?
    /// Input level, 0…1, for each chunk of audio while listening.
    var onLevel: ((Float) -> Void)?
    var onError: ((String) -> Void)?

    private let capture = WebMicCapture()
    private var recognizer: SFSpeechRecognizer?
    private var request: SFSpeechAudioBufferRecognitionRequest?
    private var task: SFSpeechRecognitionTask?
    private var lastWords: [Word] = []
    /// Bumped by `start` and `cancel`, so late callbacks from a dropped session are ignored.
    private var session = 0
    private let format = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: 16_000, channels: 1, interleaved: false)!

    override init() {
        super.init()
        capture.onReady = { [weak self] in self?.captureReady() }
        capture.onSamples = { [weak self] samples in self?.append(samples) }
        capture.onFailure = { [weak self] message in self?.fail(message) }
    }

    /// Installs the hidden web view; it has to be in the window for WebKit to keep running.
    func attach(to view: UIView) { capture.attach(to: view) }

    func toggle() { state == .idle ? start() : finish() }

    func start() {
        guard state == .idle else { return }
        guard SFSpeechRecognizer.authorizationStatus() == .authorized,
              AVAudioApplication.shared.recordPermission == .granted else {
            return fail(L("请先在 XSIME App →“语音与手写（实验）”中授予麦克风和语音识别权限", "Grant microphone and speech recognition in XSIME → Voice & handwriting (experimental) first"))
        }
        guard let recognizer = SFSpeechRecognizer(locale: Locale(identifier: "zh-CN")), recognizer.isAvailable else {
            return fail(L("语音识别暂时不可用", "Speech recognition is unavailable right now"))
        }
        let request = SFSpeechAudioBufferRecognitionRequest()
        request.shouldReportPartialResults = true
        request.addsPunctuation = true
        request.taskHint = .dictation
        if recognizer.supportsOnDeviceRecognition { request.requiresOnDeviceRecognition = true }
        self.recognizer = recognizer; self.request = request; lastWords = []
        session += 1
        let id = session
        task = recognizer.recognitionTask(with: request) { [weak self] result, error in
            let words = result.map { Self.words(in: $0.bestTranscription) }
            let isFinal = result?.isFinal ?? false
            let message = error.map { ($0 as NSError).localizedDescription }
            Task { @MainActor in
                guard let self, self.session == id else { return }
                self.recognized(words: words, isFinal: isFinal, error: message)
            }
        }
        state = .starting
        capture.start()
    }

    /// Stops listening and lets recognition deliver its final text.
    func finish() {
        guard state != .idle else { return }
        capture.stop()
        request?.endAudio()
        state = .idle
    }

    /// Stops at once and drops whatever recognition has not delivered.
    func cancel() {
        session += 1
        capture.stop()
        task?.cancel(); task = nil; request = nil; recognizer = nil
        state = .idle
    }

    private func captureReady() { if state == .starting { state = .listening } }

    private func append(_ samples: [Float]) {
        guard state != .idle, let request, !samples.isEmpty,
              let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(samples.count)) else { return }
        buffer.frameLength = AVAudioFrameCount(samples.count)
        samples.withUnsafeBufferPointer { buffer.floatChannelData![0].update(from: $0.baseAddress!, count: samples.count) }
        request.append(buffer)
        // Level for the microphone's rings: RMS in decibels, −50…−10 dB mapped to 0…1.
        let power = samples.reduce(Float(0)) { $0 + $1 * $1 } / Float(samples.count)
        let decibels = 10 * log10(max(power, 1e-10))
        onLevel?(min(1, max(0, (decibels + 50) / 40)))
    }

    private func recognized(words: [Word]?, isFinal: Bool, error: String?) {
        if let words, words != lastWords || isFinal { lastWords = words; onWords?(words, isFinal) }
        // An error ends the utterance too: freeze what was heard so far.
        if error != nil && !isFinal { onWords?(lastWords, true) }
        if isFinal || error != nil {
            // "No speech detected" ends a silent session; it is not worth an error.
            if let error, lastWords.isEmpty, state != .idle { onError?(error) }
            capture.stop(); task = nil; request = nil; recognizer = nil; state = .idle
        }
    }

    /// Splits the formatted transcription into its segments; punctuation and other text between
    /// segments becomes words of its own, without alternatives.
    nonisolated private static func words(in transcription: SFTranscription) -> [Word] {
        let full = transcription.formattedString as NSString
        var words: [Word] = [], cursor = 0
        func gap(_ range: NSRange) {
            guard range.length > 0 else { return }
            for piece in full.substring(with: range).components(separatedBy: .whitespacesAndNewlines) where !piece.isEmpty {
                words.append(Word(text: piece, alternatives: []))
            }
        }
        for segment in transcription.segments.sorted(by: { $0.substringRange.location < $1.substringRange.location }) {
            let range = segment.substringRange
            guard range.location >= cursor, NSMaxRange(range) <= full.length else { continue }
            gap(NSRange(location: cursor, length: range.location - cursor))
            let text = full.substring(with: range).trimmingCharacters(in: .whitespacesAndNewlines)
            if !text.isEmpty {
                var alternatives: [String] = []
                for alternative in segment.alternativeSubstrings where !alternative.isEmpty && alternative != text && !alternatives.contains(alternative) { alternatives.append(alternative) }
                words.append(Word(text: text, alternatives: alternatives))
            }
            cursor = NSMaxRange(range)
        }
        gap(NSRange(location: cursor, length: full.length - cursor))
        return words
    }

    private func fail(_ message: String) {
        cancel(); onError?(message)
    }
}

/// A 1×1, almost transparent WKWebView that records with `getUserMedia` and posts 16 kHz
/// mono Float32 PCM chunks (base64) to the `capture` message handler.
@MainActor private final class WebMicCapture: NSObject, WKScriptMessageHandler, WKUIDelegate, WKNavigationDelegate {
    var onReady: (() -> Void)?
    var onSamples: (([Float]) -> Void)?
    var onFailure: ((String) -> Void)?
    private var webView: WKWebView?
    private var loaded = false
    private var wantsStart = false

    func attach(to view: UIView) {
        if let webView { if webView.superview !== view { view.addSubview(webView) }; return }
        let configuration = WKWebViewConfiguration()
        configuration.allowsInlineMediaPlayback = true
        configuration.mediaTypesRequiringUserActionForPlayback = []
        // Weak proxy: WKUserContentController retains its handlers.
        configuration.userContentController.add(WeakHandler(self), name: "capture")
        let webView = WKWebView(frame: CGRect(x: 0, y: 0, width: 1, height: 1), configuration: configuration)
        webView.alpha = 0.01 // Fully hidden views get throttled.
        webView.isUserInteractionEnabled = false
        webView.isAccessibilityElement = false
        webView.uiDelegate = self; webView.navigationDelegate = self
        view.addSubview(webView)
        self.webView = webView
        // getUserMedia needs a secure context; the host never resolves.
        webView.loadHTMLString(Self.page, baseURL: URL(string: "https://keyboard-dictation.invalid/"))
    }

    func start() {
        wantsStart = true
        guard let webView else { return fail("WebView missing") }
        if loaded { webView.evaluateJavaScript("startCapture()") }
    }

    func stop() {
        wantsStart = false
        webView?.evaluateJavaScript("stopCapture()")
    }

    private func fail(_ message: String) { wantsStart = false; onFailure?(message) }

    func userContentController(_ controller: WKUserContentController, didReceive message: WKScriptMessage) {
        guard let body = message.body as? [String: Any], let type = body["type"] as? String else { return }
        switch type {
        case "loaded":
            loaded = true
            if wantsStart { webView?.evaluateJavaScript("startCapture()") }
        case "started": onReady?()
        case "audio":
            guard wantsStart, let base64 = body["data"] as? String, let data = Data(base64Encoded: base64) else { return }
            let samples = data.withUnsafeBytes { Array($0.bindMemory(to: Float.self)) }
            onSamples?(samples)
        case "error":
            fail(L("麦克风无法启动：", "Microphone failed: ") + (body["message"] as? String ?? "?"))
        default: break
        }
    }

    func webView(_ webView: WKWebView, requestMediaCapturePermissionFor origin: WKSecurityOrigin, initiatedByFrame frame: WKFrameInfo,
                 type: WKMediaCaptureType, decisionHandler: @escaping @MainActor (WKPermissionDecision) -> Void) {
        decisionHandler(type == .microphone ? .grant : .deny)
    }

    func webViewWebContentProcessDidTerminate(_ webView: WKWebView) {
        loaded = false
        if wantsStart { fail(L("录音进程已退出", "The recording process ended")) }
        webView.loadHTMLString(Self.page, baseURL: URL(string: "https://keyboard-dictation.invalid/"))
    }

    private static let page = """
    <!doctype html><html><head><meta charset="utf-8"></head><body><script>
    const post = m => window.webkit.messageHandlers.capture.postMessage(m);
    let stream = null, context = null, node = null, source = null, generation = 0;
    function toBase64(floats) {
      const bytes = new Uint8Array(floats.buffer); let s = "";
      for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
      return btoa(s);
    }
    async function startCapture() {
      if (stream) { post({type: "started"}); return; }
      // Invalidate pending getUserMedia/resume callbacks when the user stops or
      // a new recording begins. A late stream must never keep the mic open.
      const request = ++generation;
      try {
        if (navigator.audioSession) navigator.audioSession.type = "play-and-record";
        const incoming = await navigator.mediaDevices.getUserMedia({audio: {echoCancellation: true, noiseSuppression: true, autoGainControl: true, channelCount: 1}});
        if (request !== generation) { incoming.getTracks().forEach(t => t.stop()); return; }
        stream = incoming;
        context = new AudioContext();
        if (context.state !== "running") await context.resume();
        if (request !== generation) return;
        source = context.createMediaStreamSource(stream);
        node = context.createScriptProcessor(2048, 1, 1);
        const ratio = context.sampleRate / 16000; let carry = 0;
        node.onaudioprocess = e => {
          const input = e.inputBuffer.getChannelData(0);
          // Box-average each 1/16000 s window; `carry` is where the next window starts.
          const count = Math.max(0, Math.ceil((input.length - carry) / ratio));
          const out = new Float32Array(count);
          for (let i = 0; i < count; i++) {
            const start = carry + i * ratio, end = Math.min(input.length, Math.ceil(start + ratio)); let sum = 0, n = 0;
            for (let j = Math.floor(start); j < end; j++) { sum += input[j]; n++; }
            out[i] = n ? sum / n : 0;
          }
          carry = carry + count * ratio - input.length;
          if (out.length) post({type: "audio", data: toBase64(out)});
        };
        source.connect(node); node.connect(context.destination);
        stream.getAudioTracks().forEach(t => t.onended = () => {
          if (request !== generation) return;
          stopCapture(); post({type: "error", message: "track ended"});
        });
        post({type: "started"});
      } catch (e) {
        if (request !== generation) return;
        stopCapture(); post({type: "error", message: String(e && (e.name + ": " + e.message) || e)});
      }
    }
    function stopCapture() {
      ++generation;
      if (node) { node.onaudioprocess = null; node.disconnect(); node = null; }
      if (source) { source.disconnect(); source = null; }
      if (stream) { stream.getTracks().forEach(t => t.stop()); stream = null; }
      if (context) { context.close(); context = null; }
      if (navigator.audioSession) navigator.audioSession.type = "auto";
    }
    window.addEventListener("pagehide", stopCapture);
    post({type: "loaded"});
    </script></body></html>
    """
}

private final class WeakHandler: NSObject, WKScriptMessageHandler {
    weak var target: WKScriptMessageHandler?
    init(_ target: WKScriptMessageHandler) { self.target = target }
    func userContentController(_ controller: WKUserContentController, didReceive message: WKScriptMessage) {
        target?.userContentController(controller, didReceive: message)
    }
}
