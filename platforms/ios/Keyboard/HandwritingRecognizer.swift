import UIKit
import CoreML
#if KEYBOARD_LAYOUT_TESTS
@testable import RIMES
#endif

/// Handwriting recognition for the handwriting scheme.
///
/// The ink is drawn as a text-line image and read by PaddleOCR's PP-OCRv5 mobile recognition
/// model (Apache-2.0) when a converted Core ML model and dictionary are present in the bundle.
/// zinnia with the bundled Tegaki model answers when the OCR model is unavailable. Recognition
/// runs offline on the device; only the OCR model can read several characters as one line.
@MainActor final class HandwritingRecognizer {
    private let queue = DispatchQueue(label: "org.scholay.rimes.handwriting", qos: .userInitiated)
    /// Bumped by `cancel`; requests from an earlier generation are dropped.
    private var generation = 0
    /// Footprint right after recognition (development measurement).
    var onMeasured: ((UInt64) -> Void)?

    /// Calls `completion` on the main actor with up to ten candidates, best first. Requests run
    /// in order on one queue, so the tape fills front to back while the user keeps writing.
    func recognize(_ ink: [InkTrace], in size: CGSize, preContext: String, completion: @escaping @MainActor ([String]) -> Void) {
        let id = generation
        let strokes = ink.map(\.points)
        queue.async { [weak self] in
            var results = TextLineReader.shared?.read(strokes) ?? []
            if results.isEmpty { results = Self.zinnia(strokes, size: size) }
            let footprint = KeyboardMetrics.footprint()
            Task { @MainActor in
                guard let self else { return }
                self.onMeasured?(footprint)
                guard self.generation == id else { return }
                completion(results)
            }
        }
    }

    /// Drops every request still in flight.
    func cancel() { generation += 1 }

    #if !KEYBOARD_LAYOUT_TESTS
    /// Opened on first use and kept for the keyboard's life; only touched on `queue`.
    nonisolated(unsafe) private static var engine: RIMESZinniaRecognizer?
    nonisolated(unsafe) private static var engineMissing = false
    #endif

    nonisolated private static func zinnia(_ strokes: [[CGPoint]], size: CGSize) -> [String] {
        #if KEYBOARD_LAYOUT_TESTS
        return []
        #else
        if engine == nil && !engineMissing {
            let url = Bundle.main.url(forResource: "handwriting-zh_CN", withExtension: "model", subdirectory: "Handwriting")
            engine = url.flatMap { RIMESZinniaRecognizer(modelPath: $0.path) }
            engineMissing = engine == nil
        }
        guard let engine else { return [] }
        let flat = strokes.map { stroke in stroke.flatMap { [NSNumber(value: Double($0.x)), NSNumber(value: Double($0.y))] } }
        return rerank(engine.classifyStrokes(flat, width: size.width, height: size.height, limit: 20))
        #endif
    }

    /// Radicals and component forms (囗 亻 廾 …) are rarely written on their own; they follow
    /// the whole characters instead of leading.
    nonisolated static func rerank(_ candidates: [String]) -> [String] {
        let whole = candidates.filter { !components.contains($0) }, parts = candidates.filter { components.contains($0) }
        return Array((whole + parts).prefix(10))
    }
    nonisolated private static let components: Set<String> = Set("亻氵扌彳忄犭纟饣讠钅礻衤阝卩刂冖宀廴辶囗廾厶冂凵勹匚匸亠丷丨丿乚乛亅艹罒灬爫丬疒癶耒覀虍彡夂夊攵殳爿龴".map(String.init))
}

/// PP-OCRv5 mobile text-line recognition on a rendered image of the ink (Core ML).
///
/// Input: 1×3×48×320, the ink scaled to 48 px high and drawn dark on white, then normalized to
/// −1…1 and padded on the right with 0 — the same as PaddleOCR's own recognizer. Output: one row
/// of class probabilities per 8 px column; class 0 is the CTC blank, then the dictionary, then a
/// space.
final class TextLineReader: @unchecked Sendable {
    static let height = 48, width = 320
    /// Loaded once, the first time it is needed; nil when the model is not in the bundle.
    static let shared: TextLineReader? = TextLineReader()

    private let model: MLModel
    private let inputName: String, outputName: String
    private let dictionary: [String]

    private init?() {
        guard let modelURL = Bundle.main.url(forResource: "PPOCRv5MobileRec", withExtension: "mlmodelc"),
              let dictURL = Bundle.main.url(forResource: "ppocrv5_dict", withExtension: "txt"),
              let text = try? String(contentsOf: dictURL, encoding: .utf8) else { return nil }
        let configuration = MLModelConfiguration()
        configuration.computeUnits = .cpuAndNeuralEngine
        guard let model = try? MLModel(contentsOf: modelURL, configuration: configuration),
              let input = model.modelDescription.inputDescriptionsByName.keys.first,
              let output = model.modelDescription.outputDescriptionsByName.keys.first else { return nil }
        self.model = model; inputName = input; outputName = output
        dictionary = text.split(separator: "\n", omittingEmptySubsequences: false).map(String.init).filter { !$0.isEmpty }
    }

    /// Up to ten readings, best first.
    func read(_ strokes: [[CGPoint]]) -> [String] {
        guard let pixels = Self.render(strokes),
              let input = try? MLMultiArray(shape: [1, 3, NSNumber(value: Self.height), NSNumber(value: Self.width)], dataType: .float32) else { return [] }
        let plane = Self.height * Self.width
        let data = input.dataPointer.bindMemory(to: Float32.self, capacity: 3 * plane)
        for channel in 0..<3 { for index in 0..<plane { data[channel * plane + index] = pixels[index] } }
        guard let features = try? MLDictionaryFeatureProvider(dictionary: [inputName: MLFeatureValue(multiArray: input)]),
              let result = try? model.prediction(from: features),
              let output = result.featureValue(for: outputName)?.multiArrayValue else { return [] }
        return decode(output)
    }

    /// Ink → normalized grayscale (−1…1), 48×320, row-major; nil without ink.
    static func render(_ strokes: [[CGPoint]]) -> [Float32]? {
        let points = strokes.flatMap { $0 }
        guard let minX = points.map(\.x).min(), let maxX = points.map(\.x).max(),
              let minY = points.map(\.y).min(), let maxY = points.map(\.y).max() else { return nil }
        // Ink fills about 80% of the line height; a short or thin mark keeps a square-ish box.
        let inkHeight = max(maxY - minY, (maxX - minX) * 0.35, 12)
        let box = CGRect(x: minX, y: (minY + maxY - inkHeight) / 2, width: max(maxX - minX, 1), height: inkHeight)
        let scale = CGFloat(height) * 0.8 / box.height
        var drawnWidth = Int(ceil((box.width * scale) + CGFloat(height) * 0.2))
        var fit = scale
        if drawnWidth > width { fit = scale * CGFloat(width) / CGFloat(drawnWidth); drawnWidth = width }
        let margin = CGFloat(height) * 0.1
        let lineWidth = max(1.5, CGFloat(height) * 0.06)
        guard let context = CGContext(data: nil, width: drawnWidth, height: height, bitsPerComponent: 8, bytesPerRow: drawnWidth,
                                      space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGImageAlphaInfo.none.rawValue) else { return nil }
        context.setFillColor(gray: 1, alpha: 1); context.fill(CGRect(x: 0, y: 0, width: drawnWidth, height: height))
        context.translateBy(x: 0, y: CGFloat(height)); context.scaleBy(x: 1, y: -1)
        context.setStrokeColor(gray: 0, alpha: 1); context.setLineWidth(lineWidth); context.setLineCap(.round); context.setLineJoin(.round)
        let verticalOffset = (CGFloat(height) - box.height * fit) / 2
        for stroke in strokes {
            let mapped = stroke.map { CGPoint(x: ($0.x - box.minX) * fit + margin, y: ($0.y - box.minY) * fit + verticalOffset) }
            guard let first = mapped.first else { continue }
            context.move(to: first)
            if mapped.count == 1 { context.addLine(to: CGPoint(x: first.x + 0.5, y: first.y)) }
            for point in mapped.dropFirst() { context.addLine(to: point) }
            context.strokePath()
        }
        guard let bytes = context.data?.bindMemory(to: UInt8.self, capacity: drawnWidth * height) else { return nil }
        var pixels = [Float32](repeating: 0, count: height * width) // 0 is PaddleOCR's padding value
        for y in 0..<height { for x in 0..<drawnWidth { pixels[y * width + x] = Float32(bytes[y * drawnWidth + x]) / 127.5 - 1 } }
        return pixels
    }

    /// CTC greedy decoding. A single character offers the classes most probable over its
    /// columns; a longer reading offers alternatives for its least certain character.
    private func decode(_ output: MLMultiArray) -> [String] {
        let shape = output.shape.map(\.intValue)
        guard shape.count == 3 else { return [] }
        let steps = shape[1], classes = shape[2]
        let strides = output.strides.map(\.intValue)
        func probability(_ t: Int, _ c: Int) -> Float {
            let offset = t * strides[1] + c * strides[2]
            switch output.dataType {
            case .float16: return Float(output.dataPointer.bindMemory(to: Float16.self, capacity: offset + 1)[offset])
            case .float32: return output.dataPointer.bindMemory(to: Float32.self, capacity: offset + 1)[offset]
            default: return output[[0, NSNumber(value: t), NSNumber(value: c)]].floatValue
            }
        }
        func text(_ c: Int) -> String? { c >= 1 && c <= dictionary.count ? dictionary[c - 1] : nil }
        // Best path, and for each emitted character the columns it spans.
        var emitted: [(c: Int, columns: [Int])] = []
        var previous = 0
        for t in 0..<steps {
            var best = 0, bestP: Float = -1
            for c in 0..<classes { let p = probability(t, c); if p > bestP { bestP = p; best = c } }
            if best != 0 {
                if best == previous, var last = emitted.popLast() { last.columns.append(t); emitted.append(last) }
                else { emitted.append((best, [t])) }
            }
            previous = best
        }
        let characters = emitted.compactMap { text($0.c) }
        guard !characters.isEmpty, characters.count == emitted.count else { return [] }
        // Alternatives for one emitted character: the classes most probable over its columns.
        func alternatives(_ index: Int, limit: Int) -> [(String, Float)] {
            var scores: [Int: Float] = [:]
            for t in emitted[index].columns {
                var top: [(Int, Float)] = []
                for c in 1..<classes {
                    let p = probability(t, c)
                    if top.count < limit { top.append((c, p)); top.sort { $0.1 > $1.1 } }
                    else if p > top[limit - 1].1 { top[limit - 1] = (c, p); top.sort { $0.1 > $1.1 } }
                }
                for (c, p) in top { scores[c] = max(scores[c] ?? 0, p) }
            }
            return scores.sorted { $0.value > $1.value }.compactMap { c, p in text(c).map { ($0, p) } }
                .filter { !$0.0.trimmingCharacters(in: .whitespaces).isEmpty }
        }
        if characters.count == 1 {
            return HandwritingRecognizer.rerank(alternatives(0, limit: 15).map(\.0))
        }
        var results = [characters.joined()]
        let weakest = emitted.indices.min { a, b in
            (alternatives(a, limit: 1).first?.1 ?? 1) < (alternatives(b, limit: 1).first?.1 ?? 1)
        } ?? 0
        for (alternative, _) in alternatives(weakest, limit: 10) where alternative != characters[weakest] {
            var variant = characters; variant[weakest] = alternative
            let joined = variant.joined()
            if !results.contains(joined) { results.append(joined) }
            if results.count == 10 { break }
        }
        return results
    }
}
