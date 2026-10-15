import XCTest
import UIKit
import Photos
import UniformTypeIdentifiers
import RimesCore
@testable import RIMES

@MainActor final class TypingStatsCardTests: XCTestCase {
    private func png() throws -> Data { try TypingStatsCard.render(.init(characters: 8, average: 68, peak: 90, codeLength: 2.38, keysPerSecond: 2.7), chinese: true) }
    func testPNGDiskAndImageClipboardRoundTrip() throws {
        let data = try png()
        XCTAssertEqual(UIImage(data: data)?.size, CGSize(width: 1024, height: 1024))
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory) }
        let store = TypingCardStore(directory: directory)
        XCTAssertNil(try store.load()); try store.save(data); XCTAssertEqual(try store.load(), data)
        XCTAssertEqual(try directory.resourceValues(forKeys: [.isExcludedFromBackupKey]).isExcludedFromBackup, true)
        XCTAssertThrowsError(try store.save(Data("bad".utf8))); XCTAssertEqual(try store.load(), data)
        let name = UIPasteboard.Name("card-test-\(UUID().uuidString)")
        let clipboard = try XCTUnwrap(UIPasteboard(name: name, create: true)); defer { UIPasteboard.remove(withName: name) }
        try TypingStatsCard.copy(data, to: clipboard)
        XCTAssertTrue(clipboard.hasImages); XCTAssertFalse(clipboard.hasStrings)
        XCTAssertEqual(clipboard.data(forPasteboardType: UTType.png.identifier), data)
        try store.remove(); XCTAssertNil(try store.load())
    }
    func testUnknownAndLargeMetrics() throws {
        XCTAssertNil(TypingCardSnapshot(session: TypingSessionTotals()))
        for value: Double? in [nil, .nan, .infinity, -1] { XCTAssertEqual(TypingCardSnapshot.number(value, decimals: 2), "—") }
        for chinese in [true, false] {
            try TypingCardStore.validate(TypingStatsCard.render(.init(characters: Int.max, average: 1e300, peak: nil, codeLength: 2.38, keysPerSecond: 2.7), chinese: chinese))
        }
    }
    func testPhotosRequestsOnlyWhenNeededAndWritesAfterAuthorization() async throws {
        let data = try png(); var requests = 0; var written: Data?
        let service = TypingCardPhotos(authorization: { .notDetermined }, request: { requests += 1; return .authorized }, write: { written = $0 })
        try await service.save(data); XCTAssertEqual(requests, 1); XCTAssertEqual(written, data)
        written = nil
        try await TypingCardPhotos(authorization: { .authorized }, request: { XCTFail("already authorized"); return .denied }, write: { written = $0 }).save(data)
        XCTAssertEqual(written, data)
    }
    func testPhotosDenialRestrictionAndFailedWriteNeverSucceed() async throws {
        let data = try png()
        for status: PHAuthorizationStatus in [.denied, .restricted] {
            do { try await TypingCardPhotos(authorization: { status }, request: { XCTFail("Do not re-prompt"); return .authorized }, write: { _ in XCTFail("Do not write") }).save(data); XCTFail("Must fail") }
            catch { XCTAssertTrue(error is TypingCardPhotoError) }
        }
        do { try await TypingCardPhotos(authorization: { .notDetermined }, request: { .denied }, write: { _ in XCTFail("Denied") }).save(data); XCTFail("Must fail") }
        catch { XCTAssertTrue(error is TypingCardPhotoError) }
        do { try await TypingCardPhotos(authorization: { .authorized }, write: { _ in throw TypingCardPhotoError.failed }).save(data); XCTFail("Must fail") }
        catch { XCTAssertTrue(error is TypingCardPhotoError) }
    }
    func testPreviewHintOccupiesFullWidthFooter() throws {
        for size in [CGSize(width: 320, height: 252), CGSize(width: 393, height: 332), CGSize(width: 852, height: 220)] {
            let sample = TypingCardSnapshot(characters: 8, average: 68, peak: 90, codeLength: 2.38, keysPerSecond: 2.7)
            let preview = TypingCardPreview(png: try png(), fullAccess: true, blockText: TypingStatsText.matrix(sample), plainText: "8 characters", initialFormat: .image)
            preview.frame.size = size; preview.layoutIfNeeded()
            let hint = try XCTUnwrap(preview.subviews.first { $0.accessibilityIdentifier == "keyboard.typingCard.message" })
            let image = try XCTUnwrap(preview.subviews.first { $0.accessibilityIdentifier == "keyboard.typingCard.image" })
            XCTAssertEqual(hint.frame.width, size.width - 24)
            XCTAssertGreaterThan(hint.frame.minY, image.frame.maxY)
            XCTAssertEqual(hint.frame.maxY, size.height - 8)
            XCTAssertFalse(preview.hasAmbiguousLayout)
            if size.width == 393 {
                let shot = UIGraphicsImageRenderer(bounds: preview.bounds).image { preview.layer.render(in: $0.cgContext) }
                try shot.pngData()?.write(to: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("typing-card-preview.png"))
            }
        }
    }
    func testEmojiMatrixKeepsEightCellsAndAllDigits() {
        let sample = TypingCardSnapshot(characters: 8, average: 68, peak: 90, codeLength: 2.38, keysPerSecond: 2.7)
        let text = TypingStatsText.matrix(sample, chinese: true)
        let rows = text.split(separator: "\n").filter { $0.hasPrefix("⬜") }
        XCTAssertEqual(rows.count, 8)
        XCTAssertTrue(rows.allSatisfy { $0.count == 8 && $0.hasSuffix("⬜") })
        XCTAssertTrue(text.contains("⌨️2️⃣⏺️3️⃣8️⃣"))
        XCTAssertTrue(text.hasSuffix("https://github.com/scholay/xsime"))
        let large = TypingStatsText.matrix(.init(characters: 123456789, average: nil, peak: nil, codeLength: nil, keysPerSecond: nil))
        XCTAssertTrue(large.split(separator: "\n").filter { $0.hasPrefix("⬜") }.allSatisfy { $0.count == 8 })
        XCTAssertTrue(large.contains("↪️6️⃣7️⃣8️⃣9️⃣"))
        XCTAssertTrue(large.contains("📊➖"))
    }
    func testFormatChoiceUsesSelectedTextAndPhotosAction() throws {
        let preview = TypingCardPreview(png: try png(), fullAccess: true, blockText: "方块内容", plainText: "原版文字")
        func find<T: UIView>(_ id: String, _ type: T.Type) throws -> T {
            func visit(_ v: UIView) -> T? { if v.accessibilityIdentifier == id { return v as? T }; return v.subviews.lazy.compactMap(visit).first }
            return try XCTUnwrap(visit(preview))
        }
        let picker = try find("keyboard.typingCard.formats", UISegmentedControl.self)
        let primary = try find("keyboard.typingCard.primary", UIButton.self)
        var inserted = [String](), photos = 0
        preview.onText = { inserted.append($0) }; preview.onPhotos = { photos += 1 }
        XCTAssertEqual(picker.selectedSegmentIndex, TypingCardPreview.Format.blocks.rawValue)
        primary.sendActions(for: .touchUpInside)
        picker.selectedSegmentIndex = TypingCardPreview.Format.text.rawValue; picker.sendActions(for: .valueChanged)
        primary.sendActions(for: .touchUpInside)
        picker.selectedSegmentIndex = TypingCardPreview.Format.image.rawValue; picker.sendActions(for: .valueChanged)
        primary.sendActions(for: .touchUpInside)
        XCTAssertEqual(inserted, ["方块内容", "原版文字"]); XCTAssertEqual(photos, 1)
    }
}
