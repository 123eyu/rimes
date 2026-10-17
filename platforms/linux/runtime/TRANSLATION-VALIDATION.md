# Local translation resource validation — 2026-10-17

Official plugin source: `scholay/rimes-plugins` commit `fb29999`.
Host: Ubuntu 24.04 arm64 Docker container on macOS, Python 3.12, CTranslate2
4.6.0, Argos Chinese/English 1.9 models. No host user profile was used.

- Downloaded all pinned Linux arm64 wheels and both model archives, checked
  SHA-256 and size before extraction, then validated every installed file.
- Real native `rimes-translation --request` translated Chinese to English and
  English to Chinese in an isolated profile. The engine smoke repeated both
  with network proxies directed to a closed loopback port, rejected unsupported
  language and a changed model file, and repaired offline from verified cache.
- 7 resource-manager tests cover installation, restart, corruption, cancellation,
  invalid archives, missing required model files, symlinks/hardlinks and atomic
  activation. 27 Linux CTests pass, including bounded subprocess transport and
  the actual GTK resource dialog communicating with the installed build helper.
- 17 source install/version tests pass with helper/resource ownership fixtures
  and user-model preservation. The deb lifecycle test now checks installed
  helper status and model-cache retention; CI must validate the exact package.

The GUI progress and status surface is implemented. Pixel comparison against
macOS, physical desktop acceptance and Buffer continuous translation are still
open. This backend validation does not close #155 or establish a public release.
