#pragma once

// Runs against the real named-pipe Broker configured with the built-in
// my_combo profile and the isolated dictionary containing you -> 有.
void CheckFirstChord(rimes::windows::tsf::TextService* service,
                     rimes::windows::e2e::FakeContext* context,
                     rimes::windows::e2e::FakeDocument& document) {
  const auto key = [&](WPARAM vk, bool down) {
    BOOL eaten = FALSE;
    if (down) service->OnTestKeyDown(context, vk, 1, &eaten);
    else service->OnTestKeyUp(context, vk, 1, &eaten);
    Expect(eaten, "physical chord event is predicted as owned");
    if (!eaten) return;
    if (down) service->OnKeyDown(context, vk, 1, &eaten);
    else service->OnKeyUp(context, vk, 1, &eaten);
    Expect(eaten, "physical chord event stays owned through its release");
  };
  // dfkl is the production mapping for the syllable you. All keys go down
  // before any goes up; TypeLatin would test a different input path entirely.
  for (int focus = 0; focus < 2; ++focus) {
    service->OnSetFocus(FALSE);
    ResetDocument(&document);
    key('D', true);
    Expect(document.composing && document.composition == L"\u200B" && document.text.empty(),
           "first chord key establishes a host guard before Rime has preedit");
    const auto guard_writes = context->write_requests;
    for (const auto vk : {'F', 'K', 'L'}) key(vk, true);
    for (const auto vk : {'L', 'K', 'F'}) key(vk, false);
    Expect(document.composing && document.composition == L"\u200B" &&
               context->write_requests == guard_writes,
           "held chord keeps one guard without repeated host edits");
    key('D', false);
    Expect(document.composing && document.composition.find(L"you") != std::wstring::npos &&
               document.composition.find(L'\u200B') == std::wstring::npos,
           "final release replaces the guard with the mapped syllable");
    TypeVirtualKey(service, context, VK_SPACE, true);
    Expect(document.text == L"有" && !document.composing,
           "the first complete chord commits Chinese exactly once after focus entry");
  }
  ResetDocument(&document);
  key('D', true);
  TypeVirtualKey(service, context, VK_ESCAPE, true);
  Expect(!document.composing && document.text.empty(),
         "Escape clears a pending chord without committing its guard");
  key('D', false);

  key('D', true);
  service->OnSetFocus(FALSE);
  Expect(!document.composing && document.text.empty(),
         "focus loss clears the pending guard without leaving invisible text");
  context->defer_edits = true;
  key('D', true);
  Expect(!context->delayed_edits.empty(), "host may defer the first chord guard");
  service->OnSetFocus(FALSE);
  context->DrainEdits();
  context->defer_edits = false;
  Expect(!document.composing && document.text.empty(),
         "a deferred guard cannot revive a revoked input context");
  std::cout << "Native chord first-group host guard, Chinese commit and cancellation assertions finished\n";
}
