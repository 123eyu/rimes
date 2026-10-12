# iOS keyboard first-load flash — provisional host height

Investigated 2026-10-12 on iPhone 15 Pro (physical), iPad Air 5 (physical) and the
iPhone 17 Pro / iOS 26.5 simulator. Follows `keyboard-height-20261001.md`,
`keyboard-height-20261002.md` and `build48/README.md`, which left the cold-start
flicker open.

## What the flash is

On a cold start the host sizes the keyboard's hosted window three times: the full
screen, then a provisional height of its own, then the height the keyboard asked
for. The provisional step is drawn for one to three frames as a tall, empty
keyboard panel above the keys.

- The keyboard's own request was already correct and constant from its first
  layout (iPhone, chord layout: 198.9 pt throughout).
- Nothing in the extension process draws the panel: every ancestor of the input
  view has no background, and the whole `_UIHostedWindow` is resized by the host.
- The provisional height cannot be predicted. Across 38 recorded presentations on
  one phone it ranged from 394 to 672 pt, varying with the host app and with what
  was on screen before. Requesting a previously observed value matched in fewer
  than half of them.
- The host never animates a height change. One request, a request inside
  `UIView.animate`, and paced steps that each wait for the host to catch up are
  all drawn as a single cut once the changes stop.

The same sequence is reported for a minimal keyboard in
[KeyboardKit issue 1041](https://github.com/KeyboardKit/KeyboardKit/issues/1041) and
[Apple Developer Forums thread 813579](https://developer.apple.com/forums/thread/813579),
neither with a fix.

## Change

1. **Answer the provisional height.** When the provisional height arrives during
   the first presentation, the keyboard changes its request by one point and gives
   the point back 0.25 s later. In the simulator this makes the host draw its
   first frame at the requested size: the control run showed the tall panel in 3
   of 3 recordings, the nudged run showed none in the 3 recordings that captured
   the first frames. Why the host behaves this way is not established.
2. **Height independent of presentation state.** With a managed layout the bottom
   row was left out of the first request because `onscreen` is false until
   `viewWillAppear`. On the iPad this produced 189 → 227 pt 60 ms after the first
   layout. The row now depends only on the layout and `needsInputModeSwitchKey`.
3. **Less main-thread work before the host can settle.** The first
   `viewWillAppear` no longer reloads the preferences and scheme that `viewDidLoad`
   loaded moments earlier (43 ms on the phone), and the height is registered at
   the end of `viewWillAppear` as well as in `viewDidAppear`.
4. **A gap the host never closes is covered.** A placeholder directly above the
   content tracks the uncovered part of the container. It stays invisible for
   150 ms, so provisional and lagging frames never show it; a gap that outlives
   that gets the product name on the keyboard background while the height is
   requested again at 0.12, 0.3, 0.7 and 1.5 s.

## Evidence

- Simulator, frame recordings of cold starts driven by a debug launch flag (not in
  this change): control and nudged runs as above.
- iPhone 15 Pro, development build with a timestamped height trace (not in this
  change): the nudge fired in all 20 presentations after install; the host reached
  the requested height about 14 ms later. After trying that build the maintainer
  reported that the problem "is much better". The trace records sizes inside the extension,
  not what the host draws, so on-device confirmation is the maintainer's
  observation, not a captured frame.
- `KeyboardSizingTests`: the nudge, the placeholder, the bounded re-request and
  the presentation-independent height are covered against a container that
  imitates the host's lag. That fixture is not the remote keyboard host.

## Not established

- Whether the nudge removes the panel in every host app, in landscape, and on
  other devices and iOS versions.
- Whether the later 227 → 189 pt change seen once on the iPad 1.5 s after first
  display (the bottom row hiding again) still occurs; it was not reproduced after
  item 2 and its trigger was not recorded.

## Rejected approach

A branded intro was built and tested on the phone: hold the provisional height,
show the product name over the host's keyboard glass, then return to the content
height. The host kept the adopted height, but the intro always ended in a
one-frame cut because the host does not animate height changes. It is not part of
this change.
