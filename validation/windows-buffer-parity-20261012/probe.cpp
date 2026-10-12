// Read-only product diagnosis: links the current Model and layout code.
// Synthetic text only; no Broker, TSF, clipboard, settings, or network access.
#include "model.hpp"
#include "buffer_layout.hpp"
#include <iostream>

using namespace rimes::windows::workbench;

int main() {
  int reproduced = 0;
  const auto check = [&](bool ok, const char* name) {
    std::cout << (ok ? "REPRODUCED: " : "NOT REPRODUCED: ") << name << '\n';
    if (ok) ++reproduced;
  };
  {
    rimes::windows::ui::BufferPaintState state;
    state.mode = rimes::windows::ui::BufferMode::kGenerate;
    const auto layout = rimes::windows::ui::LayoutBuffer(state, 680);
    check(layout.show_result && layout.source_rail.height() > 0 &&
              layout.result_rail.height() > 0 && layout.height_dip == 105,
          "empty AI mode creates two rails (105 DIP)");
  }
  {
    Model model;
    model.Focus({101, 202, 303});
    model.Open();
    model.translate = true;
    model.Append("Unfinished source", InputKind::kCommit);
    const auto delivery = model.Send(false);
    check(delivery && !delivery->result &&
              delivery->block.text == "Unfinished source",
          "translation mode without a result sends untranslated source");
  }
  {
    Model model;
    model.Focus({101, 202, 303});
    model.Open();
    model.translate = true;
    model.Append("Original sentence.", InputKind::kCommit);
    const auto generation = model.Generate(1, true);
    const bool streamed = model.Stream(generation, 1, "First output. Second output.");
    const bool finished = model.Finish(generation, 1, true);
    const auto delivery = model.Send(false);
    if (!streamed || !finished || !delivery) return 2;
    model.Acknowledge(delivery->request, delivery->target, true);
    check(model.SourceText() == "Original sentence." &&
              model.ResultText() == "Second output.",
          "first translated child ACK leaves its original source unretired");
    model.Append("x", InputKind::kDirect);
    const bool erased = model.Backspace();
    check(erased && model.result.empty() &&
              model.SourceText() == "Original sentence.",
          "editing an unrelated tail erases remaining accepted translation");
    const auto next = model.Generate(1, true);
    check(model.busy && next.source == "Original sentence.",
          "next translation request includes source whose first child was delivered");
  }
  return reproduced == 5 ? 0 : 1;
}
