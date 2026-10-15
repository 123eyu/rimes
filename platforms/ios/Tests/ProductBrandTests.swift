import XCTest
@testable import RIMES

final class ProductBrandTests: XCTestCase {
    func testDisplayNamesAndFallback() {
        for locale in ["zh", "zh-CN", "zh-SG", "zh-Hans", "zh-Hans-HK"] {
            XCTAssertEqual(ProductBrand.name(language: locale), "犀术IME")
        }
        for locale in ["zh-Hant", "zh-TW", "zh-HK", "zh-MO", "zh_Hant_TW"] {
            XCTAssertEqual(ProductBrand.name(language: locale), "犀術IME")
        }
        for locale in ["en", "en-US", "ja", "ko", "es", ""] {
            XCTAssertEqual(ProductBrand.name(language: locale), "XSIME")
        }
    }
}
