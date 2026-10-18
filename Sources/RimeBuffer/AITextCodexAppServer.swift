import Darwin
import Foundation

/// Optional request-level model policy. Keeping this on the operation rather
/// than in the shared Codex home prevents one latency-sensitive plugin from
/// changing the user's ordinary AI connector defaults.
struct AITextCodexInferenceProfile: Equatable {
    let model: String?
    let effort: String?
    let summary: String
    let allowProviderModelFallback: Bool
    let rejectModelReroute: Bool

    static let streamInput = AITextCodexInferenceProfile(
        model: "gpt-5.6-luna",
        effort: "low",
        summary: "none",
        allowProviderModelFallback: false,
        rejectModelReroute: true
    )

    static func channel(model: String?, effort: String?) -> Self? {
        guard model != nil || effort != nil else { return nil }
        return Self(
            model: model,
            effort: effort,
            summary: "concise",
            allowProviderModelFallback: model == nil,
            rejectModelReroute: model != nil
        )
    }
}

/// Pure request construction keeps the optional inference policy auditable and
/// lets smoke tests prove that the ordinary connector's wire shape is unchanged.
private enum AITextCodexAppServerRequestShape {
    static func threadStartParameters(
        currentDirectoryURL: URL,
        inferenceProfile: AITextCodexInferenceProfile?
    ) -> [String: Any] {
        var parameters: [String: Any] = [
            "cwd": currentDirectoryURL.path,
            "approvalPolicy": "never",
            "ephemeral": true,
            "personality": "none",
        ]
        if let inferenceProfile {
            if let model = inferenceProfile.model {
                parameters["model"] = model
                parameters["allowProviderModelFallback"] =
                    inferenceProfile.allowProviderModelFallback
            }
            if let effort = inferenceProfile.effort {
                parameters["config"] = ["model_reasoning_effort": effort]
            }
        }
        return parameters
    }

    static func turnStartParameters(
        threadID: String,
        prompt: String,
        outputSchema: [String: Any],
        inferenceProfile: AITextCodexInferenceProfile?,
        mountedSkill: AITextMountedSkill? = nil,
        imagePaths: [String] = []
    ) -> [String: Any] {
        var input: [[String: Any]] = []
        if let mountedSkill {
            input.append([
                "type": "skill",
                "name": mountedSkill.name,
                "path": mountedSkill.path,
            ])
        }
        input.append(["type": "text", "text": prompt])
        for path in imagePaths {
            input.append(["type": "localImage", "path": path])
        }
        var parameters: [String: Any] = [
            "threadId": threadID,
            "input": input,
            "outputSchema": outputSchema,
            "summary": inferenceProfile?.summary ?? "concise",
        ]
        if let inferenceProfile {
            parameters["model"] = inferenceProfile.model
            parameters["effort"] = inferenceProfile.effort
        }
        return parameters
    }
}

/// One-shot Codex app-server session used by the Codex subscription connector.
/// The process is still the locally authenticated Codex CLI; app-server is used
/// because `codex exec --json` deliberately emits completed messages rather
/// than answer deltas.
final class AITextCodexAppServerOperation: AITextCancellable {
    private enum Lifecycle {
        case idle
        case starting
        case running
        case finished
    }

    private let executableURL: URL
    private let verifiedExecutable: AITextVerifiedCLIExecutable
    private let arguments: [String]
    private let environment: [String: String]
    private let currentDirectoryURL: URL
    private let prompt: String
    private let outputSchema: [String: Any]
    private let mountedSkill: AITextMountedSkill?
    private let imagePaths: [String]
    private let inferenceProfile: AITextCodexInferenceProfile?
    private let timeout: TimeInterval
    private let maximumOutputBytes: Int
    private let onEvent: (AITextProviderEvent) -> Void
    private let completion: (Result<[AITextProviderBlock], AITextProviderError>) -> Void
    private let cleanup: () -> Void

    private let stateLock = NSLock()
    private let writeLock = NSLock()
    private let parserQueue = DispatchQueue(
        label: "RimeBuffer.AIText.CodexAppServer.parser",
        qos: .userInitiated
    )
    private var lifecycle: Lifecycle = .idle
    private var process: Process?
    private var standardInput: FileHandle?
    private var timeoutWorkItem: DispatchWorkItem?
    private var protocolState: AITextCodexAppServerProtocolState
    private var lineBuffer = Data()
    private var receivedOutputBytes = 0
    private var activeThreadID: String?
    private var activeTurnID: String?
    private var processTerminationStatus: Int32?
    private var stdoutReachedEOF = false

    init(executableURL: URL,
         verifiedExecutable: AITextVerifiedCLIExecutable,
         arguments: [String],
         environment: [String: String],
         currentDirectoryURL: URL,
         prompt: String,
         outputSchema: [String: Any],
         mountedSkill: AITextMountedSkill? = nil,
         imagePaths: [String] = [],
         inferenceProfile: AITextCodexInferenceProfile? = nil,
         timeout: TimeInterval,
         maximumOutputBytes: Int,
         onEvent: @escaping (AITextProviderEvent) -> Void,
         completion: @escaping (Result<[AITextProviderBlock], AITextProviderError>) -> Void,
         cleanup: @escaping () -> Void) {
        self.executableURL = executableURL
        self.verifiedExecutable = verifiedExecutable
        self.arguments = arguments
        self.environment = environment
        self.currentDirectoryURL = currentDirectoryURL
        self.prompt = prompt
        self.outputSchema = outputSchema
        self.mountedSkill = mountedSkill
        self.imagePaths = imagePaths
        self.inferenceProfile = inferenceProfile
        let rejectsReroute = inferenceProfile?.rejectModelReroute ?? false
        protocolState = AITextCodexAppServerProtocolState(
            requiredModel: rejectsReroute ? inferenceProfile?.model : nil,
            rejectModelReroute: rejectsReroute,
            requiresImageArtifact: mountedSkill?.name == "imagegen"
        )
        self.timeout = timeout
        self.maximumOutputBytes = maximumOutputBytes
        self.onEvent = onEvent
        self.completion = completion
        self.cleanup = cleanup
    }

    func start() {
        guard JSONSerialization.isValidJSONObject(outputSchema),
              maximumOutputBytes > 0 else {
            finish(.failure(.invalidResult), stopProcess: false)
            return
        }

        let child = Process()
        let stdinPipe = Pipe()
        let stdoutPipe = Pipe()
        let stderrPipe = Pipe()
        child.executableURL = executableURL
        child.arguments = arguments
        child.environment = environment
        child.currentDirectoryURL = currentDirectoryURL
        child.standardInput = stdinPipe
        child.standardOutput = stdoutPipe
        child.standardError = stderrPipe
        child.terminationHandler = { [weak self] terminated in
            self?.parserQueue.async {
                self?.processDidExit(status: terminated.terminationStatus)
            }
        }

        stateLock.lock()
        guard lifecycle == .idle else {
            stateLock.unlock()
            return
        }
        lifecycle = .starting
        process = child
        standardInput = stdinPipe.fileHandleForWriting
        guard verifiedExecutable.stillMatches else {
            lifecycle = .finished
            process = nil
            standardInput = nil
            stateLock.unlock()
            close(stdinPipe.fileHandleForWriting)
            close(stdoutPipe.fileHandleForReading)
            close(stderrPipe.fileHandleForReading)
            cleanup()
            completion(.failure(.unavailable(
                "Codex CLI 已发生变化，请等待能力检查刷新"
            )))
            return
        }
        do {
            try child.run()
            lifecycle = .running
            stateLock.unlock()
        } catch {
            lifecycle = .finished
            process = nil
            standardInput = nil
            stateLock.unlock()
            close(stdinPipe.fileHandleForWriting)
            close(stdoutPipe.fileHandleForReading)
            close(stderrPipe.fileHandleForReading)
            cleanup()
            completion(.failure(.failed))
            return
        }

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            defer { self?.close(stdoutPipe.fileHandleForReading) }
            while true {
                let data = stdoutPipe.fileHandleForReading.availableData
                guard !data.isEmpty else { break }
                self?.parserQueue.async { self?.receiveStandardOutput(data) }
            }
            self?.parserQueue.async {
                self?.finishBufferedLine()
                self?.standardOutputDidReachEOF()
            }
        }
        DispatchQueue.global(qos: .utility).async { [weak self] in
            defer { self?.close(stderrPipe.fileHandleForReading) }
            while !stderrPipe.fileHandleForReading.availableData.isEmpty {}
        }

        let timeoutItem = DispatchWorkItem { [weak self] in self?.timeOut() }
        stateLock.lock()
        if lifecycle == .running {
            timeoutWorkItem = timeoutItem
            stateLock.unlock()
            DispatchQueue.global(qos: .utility).asyncAfter(
                deadline: .now() + max(1, timeout),
                execute: timeoutItem
            )
        } else {
            stateLock.unlock()
        }

        // External callbacks may block or re-enter. Emit only after process
        // termination, both pipe drains, and the timeout are fully armed.
        onEvent(.activity(AITextProviderActivity(
            kind: .launching,
            message: "正在启动 Codex CLI"
        )))

        sendInitialize()
    }

    func cancel() {
        let identifiers = protocolIdentifiers()
        if let threadID = identifiers.threadID,
           let turnID = identifiers.turnID {
            _ = send([
                "method": "turn/interrupt",
                "id": AITextCodexAppServerProtocolState.interruptRequestID,
                "params": ["threadId": threadID, "turnId": turnID],
            ])
        }
        finish(.failure(.cancelled))
    }

    private func timeOut() {
        let identifiers = protocolIdentifiers()
        if let threadID = identifiers.threadID,
           let turnID = identifiers.turnID {
            _ = send([
                "method": "turn/interrupt",
                "id": AITextCodexAppServerProtocolState.interruptRequestID,
                "params": ["threadId": threadID, "turnId": turnID],
            ])
        }
        finish(.failure(.timedOut))
    }

    private func sendInitialize() {
        onEvent(.activity(AITextProviderActivity(
            kind: .connecting,
            message: "正在连接 Codex"
        )))
        guard send([
            "method": "initialize",
            "id": AITextCodexAppServerProtocolState.initializeRequestID,
            "params": [
                "clientInfo": [
                    "name": "rimebuffer",
                    "title": ProductIdentity.displayName,
                    "version": "1",
                ],
            ],
        ]) else {
            finish(.failure(.failed))
            return
        }
    }

    private func sendMCPIsolationCheck() {
        guard send(["method": "initialized", "params": [:]]) else {
            finish(.failure(.failed))
            return
        }
        guard send([
            "method": "mcpServerStatus/list",
            "id": AITextCodexAppServerProtocolState.mcpStatusRequestID,
            "params": ["detail": "toolsAndAuthOnly", "limit": 100],
        ]) else {
            finish(.failure(.failed))
            return
        }
    }

    private func sendThreadStart() {
        // The process-level `rimebuffer` permission profile is authoritative.
        // Do not send the legacy sandbox shorthand here: doing so would replace
        // the profile that limits reads to this private per-request directory.
        let parameters = AITextCodexAppServerRequestShape.threadStartParameters(
            currentDirectoryURL: currentDirectoryURL,
            inferenceProfile: inferenceProfile
        )
        guard send([
            "method": "thread/start",
            "id": AITextCodexAppServerProtocolState.threadStartRequestID,
            "params": parameters,
        ]) else {
            finish(.failure(.failed))
            return
        }
    }

    private func sendTurnStart(threadID: String) {
        let parameters = AITextCodexAppServerRequestShape.turnStartParameters(
            threadID: threadID,
            prompt: prompt,
            outputSchema: outputSchema,
            inferenceProfile: inferenceProfile,
            mountedSkill: mountedSkill,
            imagePaths: imagePaths
        )
        guard send([
            "method": "turn/start",
            "id": AITextCodexAppServerProtocolState.turnStartRequestID,
            "params": parameters,
        ]) else {
            finish(.failure(.failed))
            return
        }
        onEvent(.activity(AITextProviderActivity(
            kind: .connecting,
            message: "请求已提交，等待模型"
        )))
    }

    @discardableResult
    private func send(_ object: [String: Any]) -> Bool {
        guard JSONSerialization.isValidJSONObject(object),
              let data = try? JSONSerialization.data(withJSONObject: object) else {
            return false
        }
        var record = data
        record.append(0x0A)

        writeLock.lock()
        defer { writeLock.unlock() }
        stateLock.lock()
        let handle = lifecycle == .running ? standardInput : nil
        stateLock.unlock()
        guard let handle else { return false }
        do {
            try handle.write(contentsOf: record)
            return true
        } catch {
            return false
        }
    }

    private func receiveStandardOutput(_ data: Data) {
        guard !isFinished else { return }
        guard receivedOutputBytes <= maximumOutputBytes - min(data.count, maximumOutputBytes),
              receivedOutputBytes + data.count <= maximumOutputBytes else {
            finish(.failure(.resultTooLarge))
            return
        }
        receivedOutputBytes += data.count
        lineBuffer.append(data)
        let maximumLineBytes = min(
            maximumOutputBytes,
            mountedSkill?.name == "imagegen" ? 24 * 1_048_576 : 512 * 1_024
        )
        while let newline = lineBuffer.firstIndex(of: 0x0A) {
            let record = Data(lineBuffer[..<newline])
            lineBuffer.removeSubrange(...newline)
            guard record.count <= maximumLineBytes else {
                finish(.failure(.resultTooLarge))
                return
            }
            guard !record.isEmpty else { continue }
            receiveRecord(record)
            if isFinished { return }
        }
        if lineBuffer.count > maximumLineBytes {
            finish(.failure(.resultTooLarge))
        }
    }

    private func finishBufferedLine() {
        guard !isFinished, !lineBuffer.isEmpty else { return }
        let maximumLineBytes = min(
            maximumOutputBytes,
            mountedSkill?.name == "imagegen" ? 24 * 1_048_576 : 512 * 1_024
        )
        guard lineBuffer.count <= maximumLineBytes else {
            finish(.failure(.resultTooLarge))
            return
        }
        let record = lineBuffer
        lineBuffer.removeAll(keepingCapacity: false)
        receiveRecord(record)
    }

    private func receiveRecord(_ data: Data) {
        guard let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            finish(.failure(.invalidResult))
            return
        }
        let actions = protocolState.consume(object)
        stateLock.lock()
        activeThreadID = protocolState.threadID
        activeTurnID = protocolState.turnID
        stateLock.unlock()
        for action in actions {
            if isFinished { return }
            switch action {
            case .sendMCPIsolationCheck:
                sendMCPIsolationCheck()
            case .sendThreadStart:
                sendThreadStart()
            case let .sendTurnStart(threadID):
                sendTurnStart(threadID: threadID)
            case let .activity(kind, message):
                onEvent(.activity(AITextProviderActivity(kind: kind, message: message)))
            case let .reasoningSnapshot(summary):
                onEvent(.reasoningSnapshot(summary))
            case let .textSnapshot(text):
                AITextProviderStreamingOutput.emit([text], callback: onEvent)
            case .imageGenerationStarted:
                onEvent(.imageGenerationStarted)
            case let .imageArtifactPath(path):
                onEvent(.imageArtifactPath(path))
            case let .completed(text):
                onEvent(.activity(AITextProviderActivity(
                    kind: .validating,
                    message: "正在校验生成结果"
                )))
                if mountedSkill?.name == "imagegen" {
                    finish(.success([AITextProviderBlock(
                        index: 0, text: text, title: nil
                    )]))
                    break
                }
                do {
                    finish(.success(try AITextResultDecoder.decodeFinalText(text)))
                } catch let error as AITextProviderError {
                    finish(.failure(error))
                } catch {
                    finish(.failure(.invalidResult))
                }
            case .failed:
                finish(.failure(.failed))
            }
        }
    }

    private func processDidExit(status: Int32) {
        processTerminationStatus = status
        guard stdoutReachedEOF else { return }
        failForPrematureProcessExit()
    }

    private func standardOutputDidReachEOF() {
        stdoutReachedEOF = true
        guard processTerminationStatus != nil else { return }
        failForPrematureProcessExit()
    }

    private func failForPrematureProcessExit() {
        guard !isFinished else { return }
        // A successful app-server exit without turn/completed is still an
        // incomplete protocol exchange and must fail closed.
        finish(.failure(.failed), stopProcess: false)
    }

    private var isFinished: Bool {
        stateLock.lock()
        defer { stateLock.unlock() }
        return lifecycle == .finished
    }

    private func protocolIdentifiers() -> (threadID: String?, turnID: String?) {
        stateLock.lock()
        defer { stateLock.unlock() }
        return (activeThreadID, activeTurnID)
    }

    private func finish(_ result: Result<[AITextProviderBlock], AITextProviderError>,
                        stopProcess: Bool = true) {
        stateLock.lock()
        guard lifecycle != .finished else {
            stateLock.unlock()
            return
        }
        lifecycle = .finished
        let child = process
        let input = standardInput
        process = nil
        standardInput = nil
        let timer = timeoutWorkItem
        timeoutWorkItem = nil
        stateLock.unlock()

        timer?.cancel()
        writeLock.lock()
        close(input)
        writeLock.unlock()
        if stopProcess, let child, child.isRunning {
            child.terminate()
            let pid = child.processIdentifier
            DispatchQueue.global(qos: .utility).asyncAfter(deadline: .now() + 1) {
                if child.isRunning, pid > 0 { Darwin.kill(pid, SIGKILL) }
            }
        }
        cleanup()
        completion(result)
    }

    private func close(_ handle: FileHandle?) {
        try? handle?.close()
    }
}

private enum AITextCodexAppServerProtocolAction {
    case sendMCPIsolationCheck
    case sendThreadStart
    case sendTurnStart(String)
    case activity(AITextProviderActivityKind, String)
    case reasoningSnapshot(String)
    case textSnapshot(String)
    case imageGenerationStarted
    case imageArtifactPath(String)
    case completed(String)
    case failed
}

/// Pure JSON-RPC state, separated from `Process` so split/coalesced transport
/// and lifecycle ordering can be smoke-tested without a Codex login.
struct AITextCodexAppServerProtocolState {
    static let initializeRequestID = 1
    static let mcpStatusRequestID = 2
    static let threadStartRequestID = 3
    static let turnStartRequestID = 4
    static let interruptRequestID = 5

    private(set) var threadID: String?
    private(set) var turnID: String?
    private var agentTextByID: [String: String] = [:]
    private var agentPhaseByID: [String: String] = [:]
    private var reasoningSummaryByKey: [String: String] = [:]
    private var reasoningSummaryOrder: [String] = []
    private var authoritativeFinalText: String?
    private var fallbackFinalText: String?
    private var pendingFatalError = false
    private var emittedComposing = false
    private let requiredModel: String?
    private let rejectModelReroute: Bool
    private let requiresImageArtifact: Bool
    private var completedImagePath: String?

    init(requiredModel: String? = nil,
         rejectModelReroute: Bool = false,
         requiresImageArtifact: Bool = false) {
        self.requiredModel = requiredModel
        self.rejectModelReroute = rejectModelReroute
        self.requiresImageArtifact = requiresImageArtifact
    }

    fileprivate mutating func consume(_ object: [String: Any])
        -> [AITextCodexAppServerProtocolAction] {
        if let requestID = Self.integer(object["id"]) {
            if object["error"] != nil { return [.failed] }
            switch requestID {
            case Self.initializeRequestID:
                guard object["result"] as? [String: Any] != nil else { return [.failed] }
                return [.sendMCPIsolationCheck]
            case Self.mcpStatusRequestID:
                guard let result = object["result"] as? [String: Any],
                      let servers = result["data"] as? [Any],
                      servers.isEmpty,
                      result["nextCursor"] == nil || result["nextCursor"] is NSNull else {
                    // No buffer content is sent until the application-scoped
                    // Codex home proves that it exposes no MCP server.
                    return [.failed]
                }
                return [.sendThreadStart]
            case Self.threadStartRequestID:
                guard let result = object["result"] as? [String: Any],
                      let thread = result["thread"] as? [String: Any],
                      let identifier = thread["id"] as? String,
                      !identifier.isEmpty else { return [.failed] }
                if let requiredModel {
                    // ThreadStartResponse.model is the authoritative model
                    // chosen by the provider. Validate it before returning the
                    // action that sends the user's prompt.
                    guard let actualModel = result["model"] as? String,
                          actualModel == requiredModel else { return [.failed] }
                }
                threadID = identifier
                return [
                    .activity(.connecting, "Codex 已连接"),
                    .sendTurnStart(identifier),
                ]
            case Self.turnStartRequestID:
                guard let result = object["result"] as? [String: Any],
                      let turn = result["turn"] as? [String: Any] else { return [.failed] }
                if let identifier = turn["id"] as? String, !identifier.isEmpty {
                    turnID = identifier
                }
                return [.activity(.connecting, "模型已接收请求")]
            default:
                return []
            }
        }

        guard let method = object["method"] as? String else { return [] }
        let params = object["params"] as? [String: Any] ?? [:]
        switch method {
        case "thread/started":
            if let thread = params["thread"] as? [String: Any],
               let identifier = thread["id"] as? String,
               !identifier.isEmpty {
                threadID = identifier
            }
            return []
        case "thread/status/changed":
            guard let status = params["status"] as? [String: Any],
                  status["type"] as? String == "active" else { return [] }
            return [.activity(.connecting, "Codex 已连接，模型处理中")]
        case "turn/started":
            if let turn = params["turn"] as? [String: Any],
               let identifier = turn["id"] as? String,
               !identifier.isEmpty {
                turnID = identifier
            }
            return [.activity(.connecting, "模型正在处理请求")]
        case "item/started":
            return itemStarted(params)
        case "item/agentMessage/delta":
            return agentMessageDelta(params)
        case "item/reasoning/summaryPartAdded":
            return [.activity(.reasoning, "模型正在思考")]
        case "item/reasoning/summaryTextDelta":
            return reasoningSummaryDelta(params)
        case "item/reasoning/textDelta":
            // Raw chain-of-thought is deliberately never forwarded to UI.
            return []
        case "item/completed":
            return itemCompleted(params)
        case "model/rerouted":
            if rejectModelReroute { return [.failed] }
            return [.activity(.retrying, "模型正在重新路由")]
        case "warning":
            if let message = params["message"] as? String,
               Self.looksRetryable(message) {
                return [.activity(.retrying, "连接波动，正在重试")]
            }
            return []
        case "error":
            let error = params["error"] as? [String: Any]
            let message = error?["message"] as? String ?? ""
            let willRetry = params["willRetry"] as? Bool
                ?? error?["willRetry"] as? Bool
            if willRetry == true || (willRetry == nil && Self.looksRetryable(message)) {
                return [.activity(.retrying, "连接波动，正在重试")]
            }
            pendingFatalError = true
            return []
        case "turn/completed":
            guard let turn = params["turn"] as? [String: Any],
                  let status = turn["status"] as? String else { return [.failed] }
            guard status == "completed", !pendingFatalError else {
                return [.failed]
            }
            if requiresImageArtifact {
                guard completedImagePath != nil else { return [.failed] }
                return [.completed("图片生成完成")]
            }
            guard let text = authoritativeFinalText ?? fallbackFinalText,
                  !text.isEmpty else { return [.failed] }
            return [.completed(text)]
        default:
            return []
        }
    }

    private mutating func itemStarted(_ params: [String: Any])
        -> [AITextCodexAppServerProtocolAction] {
        guard let item = params["item"] as? [String: Any],
              let type = item["type"] as? String else { return [] }
        if type == "agentMessage" {
            if let identifier = item["id"] as? String {
                agentPhaseByID[identifier] = item["phase"] as? String
            }
            guard !emittedComposing else { return [] }
            emittedComposing = true
            return [.activity(.composing, "正在生成回复")]
        }
        if type == "reasoning" {
            return [.activity(.reasoning, "模型正在思考")]
        }
        if type == "userMessage" {
            return [.activity(.connecting, "模型已收到请求")]
        }
        if type == "imageGeneration" {
            return [.imageGenerationStarted]
        }
        return []
    }

    private mutating func agentMessageDelta(_ params: [String: Any])
        -> [AITextCodexAppServerProtocolAction] {
        guard let identifier = params["itemId"] as? String,
              let delta = params["delta"] as? String,
              !delta.isEmpty else { return [] }
        let text = (agentTextByID[identifier] ?? "") + delta
        agentTextByID[identifier] = text

        if agentPhaseByID[identifier] == "commentary" {
            let summary = Self.activitySummary(text)
            return summary.isEmpty ? [] : [.activity(.composing, summary)]
        }
        return [.textSnapshot(text)]
    }

    private mutating func reasoningSummaryDelta(_ params: [String: Any])
        -> [AITextCodexAppServerProtocolAction] {
        guard let delta = params["delta"] as? String, !delta.isEmpty else {
            return [.activity(.reasoning, "模型正在思考")]
        }
        let identifier = params["itemId"] as? String ?? "reasoning"
        let summaryIndex = Self.integer(params["summaryIndex"]) ?? 0
        let key = "\(identifier):\(summaryIndex)"
        if reasoningSummaryByKey[key] == nil {
            reasoningSummaryOrder.append(key)
        }
        let summary = String(((reasoningSummaryByKey[key] ?? "") + delta).suffix(4_096))
        reasoningSummaryByKey[key] = summary
        let visible = Self.activitySummary(summary)
        let snapshot = String(reasoningSummaryOrder
            .compactMap { reasoningSummaryByKey[$0] }
            .joined(separator: "\n").suffix(4_096))
        return [
            .activity(.reasoning, visible.isEmpty ? "模型正在思考" : visible),
            .reasoningSnapshot(snapshot),
        ]
    }

    private mutating func itemCompleted(_ params: [String: Any])
        -> [AITextCodexAppServerProtocolAction] {
        guard let item = params["item"] as? [String: Any],
              let type = item["type"] as? String else { return [] }
        if type == "agentMessage",
           let text = item["text"] as? String,
           !text.isEmpty {
            let identifier = item["id"] as? String
            if let identifier { agentTextByID[identifier] = text }
            let phase = item["phase"] as? String
                ?? identifier.flatMap { agentPhaseByID[$0] }
            if phase == "final_answer" {
                authoritativeFinalText = text
            } else if phase != "commentary" {
                fallbackFinalText = text
            }
            guard phase != "commentary" else { return [] }
            return [.textSnapshot(text)]
        }
        if type == "reasoning" {
            return [.activity(.reasoning, "思考完成，正在组织回复")]
        }
        if type == "imageGeneration",
           item["status"] as? String == "completed",
           let path = item["savedPath"] as? String,
           !path.isEmpty {
            completedImagePath = path
            return [.imageArtifactPath(path)]
        }
        if type == "error",
           let message = item["message"] as? String,
           Self.looksRetryable(message) {
            return [.activity(.retrying, "连接波动，正在重试")]
        }
        return []
    }

    private static func integer(_ value: Any?) -> Int? {
        if let value = value as? Int { return value }
        return (value as? NSNumber)?.intValue
    }

    private static func looksRetryable(_ message: String) -> Bool {
        let normalized = message.lowercased()
        return normalized.contains("reconnect")
            || normalized.contains("retry")
            || normalized.contains("falling back")
            || normalized.contains("stream disconnected")
    }

    private static func activitySummary(_ raw: String) -> String {
        let compact = raw
            .replacingOccurrences(of: "\r\n", with: "\n")
            .split(whereSeparator: { $0.isNewline })
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .last(where: { !$0.isEmpty }) ?? ""
        guard compact.count > 160 else { return compact }
        return String(compact.prefix(157)) + "…"
    }
}

enum AITextCodexAppServerProtocolSmoke {
    static func run() -> Bool {
        guard requestShapesAreCorrect(), strictModelLockIsEnforced(),
              imageSkillEventsAreAuthoritative() else {
            return false
        }

        var state = AITextCodexAppServerProtocolState()
        guard containsMCPIsolationCheck(state.consume([
            "id": AITextCodexAppServerProtocolState.initializeRequestID,
            "result": ["userAgent": "smoke"],
        ])) else { return false }

        let isolationActions = state.consume([
            "id": AITextCodexAppServerProtocolState.mcpStatusRequestID,
            "result": ["data": [], "nextCursor": NSNull()],
        ])
        guard containsSendThreadStart(isolationActions) else { return false }

        var unsafeState = AITextCodexAppServerProtocolState()
        _ = unsafeState.consume([
            "id": AITextCodexAppServerProtocolState.initializeRequestID,
            "result": ["userAgent": "smoke"],
        ])
        guard unsafeState.consume([
            "id": AITextCodexAppServerProtocolState.mcpStatusRequestID,
            "result": ["data": [["name": "must-fail"]], "nextCursor": NSNull()],
        ]).contains(where: { if case .failed = $0 { return true }; return false }) else {
            return false
        }

        let threadActions = state.consume([
            "id": AITextCodexAppServerProtocolState.threadStartRequestID,
            "result": ["thread": ["id": "thread-smoke"]],
        ])
        guard state.threadID == "thread-smoke",
              containsTurnStart(threadActions, id: "thread-smoke") else { return false }

        _ = state.consume([
            "id": AITextCodexAppServerProtocolState.turnStartRequestID,
            "result": ["turn": ["id": "turn-smoke"]],
        ])
        guard state.turnID == "turn-smoke" else { return false }

        _ = state.consume([
            "method": "item/started",
            "params": ["item": [
                "type": "agentMessage",
                "id": "message-smoke",
                "text": "",
                "phase": "final_answer",
            ]],
        ])
        let first = state.consume([
            "method": "item/agentMessage/delta",
            "params": ["itemId": "message-smoke", "delta": "{\"blocks\":[{\"text\":\"流"],
        ])
        let second = state.consume([
            "method": "item/agentMessage/delta",
            "params": ["itemId": "message-smoke", "delta": "式\",\"title\":null}]}"],
        ])
        guard lastSnapshot(first) == "{\"blocks\":[{\"text\":\"流",
              lastSnapshot(second) == "{\"blocks\":[{\"text\":\"流式\",\"title\":null}]}" else {
            return false
        }

        let reasoning = state.consume([
            "method": "item/reasoning/summaryTextDelta",
            "params": ["itemId": "reasoning-smoke", "summaryIndex": 0, "delta": "正在分析"],
        ])
        guard containsActivity(reasoning, kind: .reasoning),
              lastReasoning(reasoning) == "正在分析" else { return false }
        let continuedReasoning = state.consume([
            "method": "item/reasoning/summaryTextDelta",
            "params": ["itemId": "reasoning-smoke", "summaryIndex": 0, "delta": "请求"],
        ])
        guard lastReasoning(continuedReasoning) == "正在分析请求" else { return false }

        let rawReasoning = state.consume([
            "method": "item/reasoning/textDelta",
            "params": ["itemId": "reasoning-smoke", "contentIndex": 0, "delta": "raw-secret"],
        ])
        guard rawReasoning.isEmpty else { return false }
        let retry = state.consume([
            "method": "error",
            "params": [
                "willRetry": true,
                "error": ["message": "temporary transport issue"],
            ],
        ])
        guard containsActivity(retry, kind: .retrying) else { return false }
        _ = state.consume([
            "method": "item/completed",
            "params": ["item": [
                "type": "agentMessage",
                "id": "message-smoke",
                "phase": "final_answer",
                "text": "{\"blocks\":[{\"text\":\"流式\",\"title\":null}]}",
            ]],
        ])
        let terminal = state.consume([
            "method": "turn/completed",
            "params": ["turn": ["id": "turn-smoke", "status": "completed"]],
        ])
        return terminal.contains { action in
            if case let .completed(text) = action {
                return text == "{\"blocks\":[{\"text\":\"流式\",\"title\":null}]}"
            }
            return false
        }
    }

    private static func imageSkillEventsAreAuthoritative() -> Bool {
        let mounted = AITextMountedSkill(
            name: "imagegen", path: "/private/skill/imagegen/SKILL.md"
        )
        let turn = AITextCodexAppServerRequestShape.turnStartParameters(
            threadID: "image-thread", prompt: "draw",
            outputSchema: ["type": "object"],
            inferenceProfile: nil, mountedSkill: mounted
        )
        guard let input = turn["input"] as? [[String: Any]],
              input.count == 2,
              input[0]["type"] as? String == "skill",
              input[0]["name"] as? String == "imagegen",
              input[0]["path"] as? String == mounted.path,
              input[1]["type"] as? String == "text" else { return false }

        var missingImage = AITextCodexAppServerProtocolState(
            requiresImageArtifact: true
        )
        let early = missingImage.consume([
            "method": "turn/completed",
            "params": ["turn": ["status": "completed"]],
        ])
        guard containsFailure(early) else { return false }

        var state = AITextCodexAppServerProtocolState(
            requiresImageArtifact: true
        )
        let started = state.consume([
            "method": "item/started",
            "params": ["item": ["type": "imageGeneration",
                                 "status": "in_progress"]],
        ])
        guard started.contains(where: {
            if case .imageGenerationStarted = $0 { return true }
            return false
        }) else { return false }
        let path = "/private/generated_images/smoke.png"
        let saved = state.consume([
            "method": "item/completed",
            "params": ["item": ["type": "imageGeneration",
                                 "status": "completed", "savedPath": path]],
        ])
        guard saved.contains(where: {
            if case let .imageArtifactPath(value) = $0 { return value == path }
            return false
        }) else { return false }
        let terminal = state.consume([
            "method": "turn/completed",
            "params": ["turn": ["status": "completed"]],
        ])
        return terminal.contains(where: {
            if case .completed = $0 { return true }
            return false
        })
    }

    private static func requestShapesAreCorrect() -> Bool {
        let workspaceURL = URL(fileURLWithPath: "/tmp/rimes-codex-smoke",
                               isDirectory: true)
        let schema: [String: Any] = ["type": "object"]

        let ordinaryThread = AITextCodexAppServerRequestShape.threadStartParameters(
            currentDirectoryURL: workspaceURL,
            inferenceProfile: nil
        )
        guard ordinaryThread.count == 4,
              ordinaryThread["cwd"] as? String == workspaceURL.path,
              ordinaryThread["approvalPolicy"] as? String == "never",
              ordinaryThread["ephemeral"] as? Bool == true,
              ordinaryThread["personality"] as? String == "none",
              ordinaryThread["model"] == nil,
              ordinaryThread["allowProviderModelFallback"] == nil,
              ordinaryThread["config"] == nil else { return false }

        let ordinaryTurn = AITextCodexAppServerRequestShape.turnStartParameters(
            threadID: "ordinary-thread",
            prompt: "ordinary-prompt",
            outputSchema: schema,
            inferenceProfile: nil
        )
        guard ordinaryTurn.count == 4,
              ordinaryTurn["threadId"] as? String == "ordinary-thread",
              ordinaryTurn["summary"] as? String == "concise",
              ordinaryTurn["model"] == nil,
              ordinaryTurn["effort"] == nil else { return false }

        let locked = AITextCodexInferenceProfile.streamInput
        guard locked.model == "gpt-5.6-luna",
              locked.effort == "low",
              locked.summary == "none",
              !locked.allowProviderModelFallback,
              locked.rejectModelReroute else { return false }

        let lockedThread = AITextCodexAppServerRequestShape.threadStartParameters(
            currentDirectoryURL: workspaceURL,
            inferenceProfile: locked
        )
        guard lockedThread["model"] as? String == "gpt-5.6-luna",
              lockedThread["allowProviderModelFallback"] as? Bool == false,
              let config = lockedThread["config"] as? [String: Any],
              config["model_reasoning_effort"] as? String == "low" else {
            return false
        }

        let lockedTurn = AITextCodexAppServerRequestShape.turnStartParameters(
            threadID: "locked-thread",
            prompt: "locked-prompt",
            outputSchema: schema,
            inferenceProfile: locked
        )
        guard lockedTurn["model"] as? String == "gpt-5.6-luna"
            && lockedTurn["effort"] as? String == "low"
            && lockedTurn["summary"] as? String == "none" else { return false }

        let configured = AITextCodexInferenceProfile.channel(
            model: "gpt-6-sol", effort: "high"
        )
        let configuredThread = AITextCodexAppServerRequestShape.threadStartParameters(
            currentDirectoryURL: workspaceURL, inferenceProfile: configured
        )
        let configuredTurn = AITextCodexAppServerRequestShape.turnStartParameters(
            threadID: "configured-thread", prompt: "prompt", outputSchema: schema,
            inferenceProfile: configured
        )
        guard configuredThread["model"] as? String == "gpt-6-sol",
              (configuredThread["config"] as? [String: String])?["model_reasoning_effort"] == "high",
              configuredTurn["model"] as? String == "gpt-6-sol",
              configuredTurn["effort"] as? String == "high" else { return false }

        let defaultModel = AITextCodexInferenceProfile.channel(
            model: nil, effort: "medium"
        )
        let defaultThread = AITextCodexAppServerRequestShape.threadStartParameters(
            currentDirectoryURL: workspaceURL, inferenceProfile: defaultModel
        )
        return defaultThread["model"] == nil
            && (defaultThread["config"] as? [String: String])?["model_reasoning_effort"] == "medium"
    }

    private static func strictModelLockIsEnforced() -> Bool {
        guard let model = AITextCodexInferenceProfile.streamInput.model else {
            return false
        }

        var missingModel = AITextCodexAppServerProtocolState(
            requiredModel: model,
            rejectModelReroute: true
        )
        let missingActions = missingModel.consume([
            "id": AITextCodexAppServerProtocolState.threadStartRequestID,
            "result": ["thread": ["id": "must-not-start"]],
        ])
        guard containsFailure(missingActions), missingModel.threadID == nil else {
            return false
        }

        var mismatchedModel = AITextCodexAppServerProtocolState(
            requiredModel: model,
            rejectModelReroute: true
        )
        let mismatchActions = mismatchedModel.consume([
            "id": AITextCodexAppServerProtocolState.threadStartRequestID,
            "result": [
                "model": "gpt-5.6-luna-fallback",
                "thread": ["id": "must-not-start"],
            ],
        ])
        guard containsFailure(mismatchActions), mismatchedModel.threadID == nil else {
            return false
        }

        var exactModel = AITextCodexAppServerProtocolState(
            requiredModel: model,
            rejectModelReroute: true
        )
        let exactActions = exactModel.consume([
            "id": AITextCodexAppServerProtocolState.threadStartRequestID,
            "result": [
                "model": model,
                "thread": ["id": "locked-thread"],
            ],
        ])
        guard exactModel.threadID == "locked-thread",
              containsTurnStart(exactActions, id: "locked-thread") else { return false }

        let rerouted = exactModel.consume([
            "method": "model/rerouted",
            "params": ["fromModel": model, "toModel": "fallback"],
        ])
        guard containsFailure(rerouted) else { return false }

        var ordinary = AITextCodexAppServerProtocolState()
        let ordinaryReroute = ordinary.consume([
            "method": "model/rerouted",
            "params": ["fromModel": "default", "toModel": "fallback"],
        ])
        return containsActivity(ordinaryReroute, kind: .retrying)
    }

    private static func containsSendThreadStart(
        _ actions: [AITextCodexAppServerProtocolAction]
    ) -> Bool {
        actions.contains { if case .sendThreadStart = $0 { return true }; return false }
    }

    private static func containsFailure(
        _ actions: [AITextCodexAppServerProtocolAction]
    ) -> Bool {
        actions.contains { if case .failed = $0 { return true }; return false }
    }

    private static func containsMCPIsolationCheck(
        _ actions: [AITextCodexAppServerProtocolAction]
    ) -> Bool {
        actions.contains {
            if case .sendMCPIsolationCheck = $0 { return true }
            return false
        }
    }

    private static func containsTurnStart(
        _ actions: [AITextCodexAppServerProtocolAction],
        id: String
    ) -> Bool {
        actions.contains { action in
            if case let .sendTurnStart(value) = action { return value == id }
            return false
        }
    }

    private static func lastSnapshot(
        _ actions: [AITextCodexAppServerProtocolAction]
    ) -> String? {
        actions.reversed().compactMap { action in
            if case let .textSnapshot(text) = action { return text }
            return nil
        }.first
    }

    private static func lastReasoning(
        _ actions: [AITextCodexAppServerProtocolAction]
    ) -> String? {
        actions.reversed().compactMap { action in
            if case let .reasoningSnapshot(text) = action { return text }
            return nil
        }.first
    }

    private static func containsActivity(
        _ actions: [AITextCodexAppServerProtocolAction],
        kind: AITextProviderActivityKind
    ) -> Bool {
        actions.contains { action in
            if case let .activity(value, _) = action { return value == kind }
            return false
        }
    }
}

/// Public synthetic replay from the actual macOS parser and request builders.
enum AITextCodexLinuxProtocolFixture {
    static var value: [String: Any] {
        let profile = AITextCodexInferenceProfile.channel(model: "gpt-6-sol", effort: "high")
        let prefix: [[String: Any]] = [
            ["id": 1, "result": [:]],
            ["id": 2, "result": ["data": [], "nextCursor": NSNull()]],
            ["id": 3, "result": ["thread": ["id": "public-thread"], "model": "gpt-6-sol"]],
            ["id": 4, "result": ["turn": ["id": "public-turn"]]],
        ]
        let scenarios: [(String, [[String: Any]])] = [
            ("text", prefix + [
                ["method": "item/started", "params": ["item": ["type": "agentMessage", "id": "a", "phase": "final_answer"]]],
                ["method": "item/agentMessage/delta", "params": ["itemId": "a", "delta": "公开"]],
                ["method": "item/agentMessage/delta", "params": ["itemId": "a", "delta": "回复"]],
                ["method": "item/completed", "params": ["item": ["type": "agentMessage", "id": "a", "phase": "final_answer", "text": "公开回复"]]],
                ["method": "turn/completed", "params": ["turn": ["id": "public-turn", "status": "completed"]]],
            ]),
            ("commentary-is-not-answer", prefix + [
                ["method": "item/started", "params": ["item": ["type": "agentMessage", "id": "a", "phase": "commentary"]]],
                ["method": "item/agentMessage/delta", "params": ["itemId": "a", "delta": "处理中"]],
                ["method": "item/completed", "params": ["item": ["type": "agentMessage", "id": "a", "phase": "commentary", "text": "处理中"]]],
                ["method": "turn/completed", "params": ["turn": ["id": "public-turn", "status": "completed"]]],
            ]),
            ("mcp-must-be-empty", Array(prefix.prefix(1)) + [["id": 2, "result": ["data": [["name": "unexpected"]]]]]),
            ("model-must-match", Array(prefix.prefix(2)) + [["id": 3, "result": ["thread": ["id": "public-thread"], "model": "other"]]]),
            ("reroute-rejected", prefix + [["method": "model/rerouted", "params": [:]]]),
            ("interrupted", prefix + [["method": "turn/completed", "params": ["turn": ["id": "public-turn", "status": "interrupted"]]]]),
        ]
        let replays: [[String: Any]] = scenarios.map { name, events in
            var state = AITextCodexAppServerProtocolState(requiredModel: "gpt-6-sol", rejectModelReroute: true)
            let outputs: [[String]] = events.map { event in
                state.consume(event).compactMap { action in
                    switch action {
                    case .sendMCPIsolationCheck: return "mcp"
                    case .sendThreadStart: return "thread"
                    case .sendTurnStart(let id): return "turn:" + id
                    case .textSnapshot(let text): return "text:" + text
                    case .completed(let text): return "done:" + text
                    case .failed: return "failed"
                    default: return nil
                    }
                }
            }
            return ["name": name, "events": events, "outputs": outputs]
        }
        return [
            "thread": AITextCodexAppServerRequestShape.threadStartParameters(currentDirectoryURL: URL(fileURLWithPath: "/RIMES_WORKSPACE"), inferenceProfile: profile),
            "turn": AITextCodexAppServerRequestShape.turnStartParameters(threadID: "public-thread", prompt: "公开请求", outputSchema: ["type": "object"], inferenceProfile: profile),
            "replays": replays,
        ]
    }
}
