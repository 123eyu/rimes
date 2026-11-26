# zinnia

Online handwriting recognition engine by Taku Kudo, used by the keyboard's experimental
handwriting scheme. New BSD License; see `COPYING`.

- Source: https://github.com/taku910/zinnia, commit `581faa8f6f15e4a7b21964be3a5ec36265c80e5b`
  (directory `zinnia/`). Files are copied unmodified; the command-line tools
  (`zinnia.cpp`, `zinnia_learn.cpp`, `zinnia_convert.cpp`) are left out.
- `config.h` is the upstream generated POSIX configuration; the files are compiled as C++11 (upstream predates C++17) with
  `-DHAVE_CONFIG_H`.

The recognition model is not vendored here. `scripts/bootstrap.py` fetches the Tegaki
Simplified Chinese light model (LGPL-2.1, 6,763 GB2312 characters) pinned in
`dependencies.lock.json` and extracts it to `Resources/Handwriting/`.
