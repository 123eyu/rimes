# Windows settings home — 2026-10-17

The Windows settings home follows `SettingsRouting.swift` and `SettingsWindow.swift` on macOS. It is the first sidebar item, uses a house icon and the localized product name, and opens by default. Existing page IDs and explicit deep links remain stable.

- Brand page: bundled product logo, localized name, the same Chinese/English introduction, compiled product version, and 学术海 / librime credit.
- Six tabs: product, 官网, 邮箱, 开源项目, README, License. The five destinations match macOS. Links open only on an explicit native button action, via the default browser/mail handler; failure displays the destination for manual use.
- Native buttons retain Tab/Enter operation. Closing and reopening starts at home. Existing settings drafts still use the ordinary Save/Close lifecycle.

Validation:

- Release builds of `RimesBroker`, `RimesVisualPreview`, `RimesSettingsUITests` passed for x64 and x86.
- `RimesSettingsUi` passed on both architectures. Coverage includes default opening, navigation order, all five link dispatches, keyboard activation, no stale link activation after leaving home, reopening, and existing settings/runtime preservation checks.
- Native control bounds checks cover all subpages at 860×600 and 980×680. Preview export covers all four themes and all six home tabs; the attached screenshots are rendered from production UI code, not desktop captures.
- `git diff --check` passed. The local worktree includes earlier Windows work and remains uncommitted; the install package records the working source snapshot.

Local update identity and retained-data checks are in `installed-result.json` and `post-install-check.json`. Both TSF DLLs are byte-identical to the previous installation, so this UI update takes effect with the Broker restart without Windows sign-out. The installer can retain an older `requiresSignOut` flag; this does not indicate a new DLL change here.

Screenshots: [dark home](home-night.png), [light home](home-day.png), [License](license-day.png).
