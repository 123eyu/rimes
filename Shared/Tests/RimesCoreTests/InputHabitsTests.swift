import XCTest
@testable import RimesCore

final class InputHabitsTests: XCTestCase {
    func testIndependentSchemesAndResetPreserveOtherEdits() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        let first = InputHabitsStore(root: root), second = InputHabitsStore(root: root)
        XCTAssertTrue(first.load().schemes.isEmpty)
        try first.set(true, schema: "builtin:wubi86", option: "rimes_wubi_manual_commit")
        try second.set(false, schema: "imported:wubi86", option: "future_option")
        try first.set(true, schema: "builtin:wubi86", option: "rimes_wubi_fifth_code")
        try first.set(nil, schema: "builtin:wubi86", option: "rimes_wubi_manual_commit")
        let actual = second.load()
        XCTAssertNil(actual["builtin:wubi86", "rimes_wubi_manual_commit"])
        XCTAssertEqual(actual["builtin:wubi86", "rimes_wubi_fifth_code"], true)
        XCTAssertEqual(actual["imported:wubi86", "future_option"], false)
        XCTAssertEqual(actual.version, 1)
    }
    func testInvalidAndFutureFilesAreNotOverwritten() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        let store = InputHabitsStore(root: root)
        for source in ["broken", "{\"version\":2,\"schemes\":{}}", "{\"version\":1,\"schemes\":{\"x\":{\"a\":\"true\"}}}"] {
            let data = Data(source.utf8); try data.write(to: store.file)
            XCTAssertThrowsError(try store.set(true, schema: "builtin:wubi86", option: "a"))
            XCTAssertEqual(try Data(contentsOf: store.file), data)
        }
    }
}
