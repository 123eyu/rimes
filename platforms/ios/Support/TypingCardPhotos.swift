import Photos
import UniformTypeIdentifiers

enum TypingCardPhotoError: LocalizedError {
    case denied, restricted, failed
    var errorDescription: String? {
        switch self {
        case .denied: return L("未获得相册添加权限。请在系统设置 → \(ProductBrand.name) → 照片中允许添加照片后重试。", "Photo access was denied. Allow adding photos in Settings → \(ProductBrand.name) → Photos, then try again.")
        case .restricted: return L("设备限制了相册权限，图片未保存。", "Photo access is restricted on this device. The image was not saved.")
        case .failed: return L("图片未能保存到相册，请重试。", "Could not save the image to Photos. Try again.")
        }
    }
}

/// Requests add-only permission on an explicit save, never reads the library.
struct TypingCardPhotos {
    var authorization: () -> PHAuthorizationStatus = { PHPhotoLibrary.authorizationStatus(for: .addOnly) }
    var request: () async -> PHAuthorizationStatus = { await PHPhotoLibrary.requestAuthorization(for: .addOnly) }
    var write: (Data) async throws -> Void = { png in
        try await PHPhotoLibrary.shared().performChanges {
            let options = PHAssetResourceCreationOptions()
            options.uniformTypeIdentifier = UTType.png.identifier
            options.originalFilename = "XSIME-typing-card.png"
            PHAssetCreationRequest.forAsset().addResource(with: .photo, data: png, options: options)
        }
    }
    func save(_ png: Data) async throws {
        try TypingCardStore.validate(png)
        let current = authorization()
        let status = current == .notDetermined ? await request() : current
        switch status {
        case .authorized, .limited: try await write(png)
        case .restricted: throw TypingCardPhotoError.restricted
        default: throw TypingCardPhotoError.denied
        }
    }
}
