import Foundation

/// The host side of lexicon learning. The 词库 page owns only the immutable
/// base dictionaries; everything about learned words (status, import/export,
/// the daily record, review and deletion) belongs to the built-in
/// lexicon-review extension, which registers itself here while it is enabled.
///
/// With no provider registered the host shows base dictionaries only and runs
/// no learning work of its own.
protocol LexiconLearningProvider: AnyObject {
    /// Settings route that hosts the extension's own pages.
    var settingsRouteID: SettingsRouteID { get }
    func status(for kind: UserLexiconKind) -> UserLexiconStatus
    /// Called on the main queue once Rime is running, or at registration if
    /// it already is.
    func engineDidStart()
}

final class LexiconLearningBridge {
    static let shared = LexiconLearningBridge()

    private(set) var provider: LexiconLearningProvider?
    private var engineStarted = false

    func register(_ provider: LexiconLearningProvider) {
        onMain {
            self.provider = provider
            if self.engineStarted { provider.engineDidStart() }
        }
    }

    /// Only the registered provider may withdraw itself, so a stale stop
    /// cannot remove a newer registration.
    func unregister(_ provider: LexiconLearningProvider) {
        onMain {
            guard self.provider === provider else { return }
            self.provider = nil
        }
    }

    /// Rime lifecycle signal from the engine; the extension decides what, if
    /// anything, to schedule.
    func engineDidStart() {
        onMain {
            guard !self.engineStarted else { return }
            self.engineStarted = true
            self.provider?.engineDidStart()
        }
    }

    private func onMain(_ work: @escaping () -> Void) {
        if Thread.isMainThread { work() } else { DispatchQueue.main.async(execute: work) }
    }
}
