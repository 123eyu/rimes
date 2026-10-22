import Foundation
import RimesCore

func mobileInputHabitsStore() -> InputHabitsStore {
    InputHabitsStore(root: FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: ConfigurationStore.groupID)
        ?? FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0])
}
