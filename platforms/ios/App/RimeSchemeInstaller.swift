import Foundation
import UIKit

enum RimeSchemeInstaller {
    /// Compilation happens in the main app on a worker, before publishing an immutable package.
    @MainActor static func install(review: RimeSchemeImportReview, selectedSchemaIDs: [String],
                        store: RimeSchemeStore = .init(),
                        progress: @escaping @Sendable (String) -> Void) async throws -> RimeSchemePackage {
        let previousIdleTimer = UIApplication.shared.isIdleTimerDisabled
        UIApplication.shared.isIdleTimerDisabled = true
        defer { UIApplication.shared.isIdleTimerDisabled = previousIdleTimer }
        return try await Task.detached(priority: .userInitiated) {
            progress("正在准备方案与词典…")
            let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: selectedSchemaIDs, destinationRoot: store.stagingRoot)
            let deploymentIDs = staged.deploymentSchemaIDs
            var published = false
            defer { if !published { try? FileManager.default.removeItem(at: staged.rootURL) } }
            try Task.checkCancellation()
            progress("正在编译词典与输入方案，请保持 App 在前台；大词库首次部署可能需要几分钟…")
            let result = RimeMobile.deployResources(staged.rootURL.path, schemaIDs: deploymentIDs)
            guard result["success"] as? Bool == true else {
                let failures = (result["failures"] as? [String] ?? []).joined(separator: "、")
                throw RimeSchemeError.invalid("Rime 部署失败：\(failures)。原输入方案没有改变。")
            }
            try Task.checkCancellation()
            progress("正在检查部署结果…")
            var artifactBytes: Int64 = 0
            guard let enumerator = FileManager.default.enumerator(at: staged.rootURL, includingPropertiesForKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey]) else {
                throw RimeSchemeError.invalid("无法检查部署目录。")
            }
            while let file = enumerator.nextObject() as? URL {
                let attributes = try file.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey])
                guard attributes.isSymbolicLink != true else { throw RimeSchemeError.invalid("部署结果包含不支持的链接。") }
                if attributes.isRegularFile == true { artifactBytes += Int64(attributes.fileSize ?? 0) }
                guard UInt64(artifactBytes) <= RimeSchemeImportService.maximumDeployedSize else { throw RimeSchemeError.invalid("部署结果超过 8 GiB。") }
                try FileManager.default.setAttributes([.protectionKey: FileProtectionType.completeUntilFirstUserAuthentication], ofItemAtPath: file.path)
            }
            for id in deploymentIDs {
                let compiled = staged.rootURL.appendingPathComponent("build/\(id).schema.yaml")
                guard FileManager.default.fileExists(atPath: compiled.path) else { throw RimeSchemeError.invalid("没有生成 \(id) 的可用方案。") }
            }
            let validation = RimeMobile.validateResources(staged.rootURL.path, schemaIDs: staged.schemaIDs)
            guard validation["success"] as? Bool == true else {
                let failures = (validation["failures"] as? [String] ?? []).joined(separator: "\n")
                throw RimeSchemeError.invalid("方案编译后未能正常加载：\n\(failures)")
            }
            let package = RimeSchemePackage(id: staged.id, name: review.sourceName, schemas: staged.schemas,
                importedAt: Date(), sourceDigest: staged.sourceDigest, warnings: staged.warnings)
            try store.publish(stagedURL: staged.rootURL, package: package)
            published = true
            progress("部署完成，尚未切换当前输入方案。")
            return package
        }.value
    }
}
