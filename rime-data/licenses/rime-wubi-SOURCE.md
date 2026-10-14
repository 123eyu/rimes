# Rime Wubi 86 source notice

- Primary upstream: https://github.com/rime/rime-wubi
- Source revision: `152a0d3f3efe40cae216d1e3b338242446848d07`
- Imported assets:
  - `wubi86.dict.yaml`
  - `wubi86.schema.yaml`
- Upstream license: GNU Lesser General Public License version 3 only
- License text: `rime-wubi-LICENSE` in this directory

The dictionary is redistributed verbatim. Its SHA-256 at import is
`f833d86b72341fe82e069a425b6625f29ef85f1bc0f34f6fb7975fe514888b5a`.

RIMES modifies `wubi86.schema.yaml` to reuse the bundled `rime_ice`
dictionary for full-pinyin reverse lookup after `z`, and displays the original
Wubi codes in parentheses. `lingxi_wubi.schema.yaml` additionally tags ordinary
alphabetic input for simultaneous Wubi and full-pinyin lookup, following the
upstream `wubi_pinyin` convention. Complete Wubi matches retain priority; `z`
explicitly requests pinyin lookup in either scheme. The Wubi dictionary and
learning database identity remain `wubi86`.

The corresponding source schemas are shipped beside the dictionary. Mobile
schemas are generated from them by `platforms/ios/scripts/prepare-data.py`,
preserving the existing `rimes_wubi` identity and using the bundled
`pinyin_simp` dictionary / `rimes_pinyin` prism. These schema adaptations retain
the upstream LGPL-3.0-only license. The original Wubi schema SHA-256 is
`cdb5aac1a9aa071552d5fdffdfe5a6618b429b19358b8b1e003130e835d5a166`.

The upstream attribution records that the dictionary is derived from
ibus-table work by Yu Yuwei and from the original Wubi work by Wang Yongmin;
the file headers and the upstream `AUTHORS` history remain authoritative.
