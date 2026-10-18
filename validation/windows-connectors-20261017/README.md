# Windows settings, connectors and host shortcuts — 2026-10-17

Installed version: `1.1.5-win-connectors.20261017`.

Active directory:
`C:\Program Files\RIMES\versions\1.1.5-win-connectors.20261017-823c910b600c-e872e11c9c73`.

Source commit: `823c910b600c14f674e41ac906435eea0757972f`, with the existing local
Windows work and this update included in the immutable package's `LOCAL-SOURCE`.
Source snapshot: `0ab435f118b542f52a1e7c8d7176194fc324d8d3cefb9537a341e66f01f68ac4`.
OfficialPlugins remains on its prior local revision; no submodule changes in this update.

## Verified

- x64 and x86 builds completed. Both architectures passed RimesRuntime,
  RimesConnections, RimesCodexConnector, RimesLocalGateway, RimesSettingsUi,
  RimesBufferLayout and RimesBrokerClientContract (7/7 each).
- Native Capsule/Mailbox fixtures passed on both architectures, including the
  first-press-open / second-press-close path. Buffer tests cover closing while
  capture is paused and reopening on the next press.
- Real librime/TSF chord fixtures passed on both architectures, including first
  group host protection, Chinese commit and cancellation. This does not replace
  the user's real Codex testing; their previous first-group fix was already confirmed.
- The settings fixture passed and exported all four themes, including API
  Provider, Codex authorization, and the gateway's client configurations.
  Representative screenshots are retained beside this record; all images use
  fixture data and redacted gateway configuration.
- DeepSeek model discovery and one synthetic generation succeeded. The supplied
  key is in Windows Credential Manager, bound to `https://api.deepseek.com` and
  profile `local-deepseek`; no key was saved in repository files. Discovery
  returned `deepseek-flash` and `deepseek-v4-pro`; the default is `deepseek-flash`.
- The user completed official Codex browser authorization. Official CLI status
  reported ChatGPT login, and the production connector returned the expected
  synthetic text using the private RIMES Codex home. Personal Codex credentials
  were not copied or inspected.
- AI and ChatGPT plugins were enabled through the verified official plugin store.
- Installation verified both COM/TSF architectures and installed hashes. One
  Broker was running from the active directory (PID 102160 at verification).
  `GET http://127.0.0.1:47700/v1/health` succeeded; the only listener was loopback
  and belonged to that Broker.
- Settings comparison found only the intended changes: `active_connection`,
  `base_url`, `connections`, `gateway_enabled`, and `model`. Schema remains
  `my_combo`, Chinese mode remains active, autostart is retained. The configured
  Buffer shortcut remains Alt+Shift+B.

Backup:
`C:\Users\young\AppData\Local\RIMES\backups\before-connectors-20261017-154110`.

The installer retains a pending-restart/sign-out marker for old DLLs mapped in
already running applications. The new Broker is active. Reopen Codex and other
test hosts to load the newly registered TSF DLL before testing Buffer plugin
cycling; no user applications were terminated and Windows was not signed out.

See `platforms/windows/native/CONNECTORS.md` for the implementation boundaries
and repeatable test targets. Streamed gateway items become reviewable after
stream completion; the exposed Windows text adapters are API Provider and Codex.
