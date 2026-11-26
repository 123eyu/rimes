import Foundation
import Darwin

/// Development-only aggregate timings. Never records keys, host identifiers or text.
@MainActor final class KeyboardMetrics {
    #if DEBUG
    private var presentationMilliseconds: Double?
    private var samples = [Double]()
    private var peakBytes: UInt64 = 0
    private var layoutSamples = [[String: Double]]()
    private var memoryMarks = [[String: Any]]()
    #endif
    /// Development-only memory checkpoint for the experimental schemes: footprint now, plus what
    /// the system says is left before this extension is killed (their sum is the limit).
    func mark(_ label: String, footprintBytes: UInt64? = nil) {
        #if DEBUG
        let now = footprintBytes ?? Self.footprint()
        memoryMarks.append(["label": label, "uptime": ProcessInfo.processInfo.systemUptime,
                            "footprintMB": Double(now) / 1_048_576, "availableMB": Double(os_proc_available_memory()) / 1_048_576])
        if memoryMarks.count > 300 { memoryMarks.removeFirst(memoryMarks.count - 300) }
        peakBytes = max(peakBytes, now)
        let root = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        try? FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        try? JSONSerialization.data(withJSONObject: memoryMarks, options: [.prettyPrinted]).write(to: root.appendingPathComponent("extra-scheme-memory.json"), options: [.atomic])
        #endif
    }
    nonisolated static func footprint() -> UInt64 {
        var info = task_vm_info_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<integer_t>.size)
        let status = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) { task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count) }
        }
        return status == KERN_SUCCESS ? info.phys_footprint : 0
    }
    func presented(since start: TimeInterval) {
        #if DEBUG
        if presentationMilliseconds == nil { presentationMilliseconds = (ProcessInfo.processInfo.systemUptime - start) * 1000 }
        sampleMemory(); save()
        #endif
    }
    func processed(since start: TimeInterval) {
        #if DEBUG
        samples.append((ProcessInfo.processInfo.systemUptime - start) * 1000)
        if samples.count > 1000 { samples.removeFirst(samples.count - 1000) }
        sampleMemory()
        if samples.count % 20 == 0 { save() }
        #endif
    }
    func sampleMemory() {
        #if DEBUG
        var info = task_vm_info_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<integer_t>.size)
        let status = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) { task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count) }
        }
        if status == KERN_SUCCESS { peakBytes = max(peakBytes, info.phys_footprint) }
        #endif
    }
    func laidOut(width: Double, height: Double, requestedHeight: Double,
                 containerHeight: Double, topInset: Double) {
        #if DEBUG
        let sample = ["width": width, "height": height, "requestedHeight": requestedHeight,
                      "containerHeight": containerHeight, "topInset": topInset]
        guard sample.values.allSatisfy(\.isFinite), layoutSamples.last != sample else { return }
        layoutSamples.append(sample)
        if layoutSamples.count > 32 { layoutSamples.removeFirst(layoutSamples.count - 32) }
        save()
        #endif
    }
    func save() {
        #if DEBUG
        let sorted = samples.sorted()
        var report: [String: Any] = ["build": Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") ?? "", "sampleCount": samples.count, "peakSampledPhysicalFootprintBytes": peakBytes, "measurement": "controller initialization to viewDidAppear; synchronous input processing; sampled footprint. Not OS launch latency or display-frame P95."]
        if let presentationMilliseconds { report["controllerPresentationMilliseconds"] = presentationMilliseconds }
        report["layoutSamples"] = layoutSamples
        if !sorted.isEmpty { report["inputProcessingP95Milliseconds"] = sorted[min(sorted.count - 1, Int(ceil(Double(sorted.count) * 0.95)) - 1)] }
        let root = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        try? FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        try? JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys]).write(to: root.appendingPathComponent("keyboard-metrics.json"), options: [.atomic, .completeFileProtection])
        #endif
    }
}
