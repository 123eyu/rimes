import Foundation

enum ProductResources {
    static let bundle: Bundle = {
        for root in [Bundle.main.resourceURL, Bundle.main.bundleURL].compactMap({ $0 }) {
            if let bundle = Bundle(url: root.appendingPathComponent("RimeBuffer_RimeBuffer.bundle")) {
                return bundle
            }
        }
        // SwiftPM's executable accessor can fall back to an absolute build
        // path and fatalError when that path is absent on an installed Mac.
        // An optional asset must remain optional even if its bundle is missing.
        return Bundle.main
    }()
}
