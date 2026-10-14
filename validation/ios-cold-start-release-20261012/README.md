# iOS 1.1.2 (55): correct the cold-start release source

TestFlight build 54 was built from `5db23ee` and omitted the newer cold-start fix `36017c0a`, subsequently merged into main. The exported archive and its frozen manifest match that older source; this was a source selection omission, not evidence that Apple distributed a different IPA.

Build 55 starts from current main `6b90e48`, including the provisional-height response, presentation-independent bottom-row height, earlier height registration and delayed placeholder reveal. It retains imported native nine-key support and the owned-mark cleanup/offscreen recovery guards. Only iOS build anchors and release records change in this publication patch.

The exact hosted build-55 suite passed **249 tests: 247 passed, 2 optional skips, 0 failures**, including the actual updated Wanxiang ZIP and cold-start/lifecycle tests. Device Release archive succeeded. At source-record time, store export is pending Xcode account reauthentication; upload, TestFlight distribution and App Review must be read back separately. Public publication receipts are supplied with the corresponding release entry.

The previous [cold-start investigation](../../platforms/ios/validation/keyboard-height-20261012.md) records simulator frame checks and a physical-phone improvement report for the existing fix. These are not physical acceptance of build 55, and do not establish that every device/host is completely free of startup flicker.
