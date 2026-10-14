#!/usr/bin/env python3
"""Repair pinned Rime's final table allocation on Linux and Android."""
from pathlib import Path
import sys


def repair_table_image_relocation(source):
    """Keep table pointers valid when a small dictionary grows its mmap.

    Rime 1.17's final string-table allocation can exceed its initial estimate.
    On Linux/Android the enlarged mapping may move, invalidating the cached pointers
    before the format header is written. Relative on-disk pointers remain valid.
    """
    path = source / 'src/rime/dict/table.cc'
    original = '''  size_t image_size = string_table_builder_->BinarySize();
  char* image = Allocate<char>(image_size);
  if (!image) {
    LOG(ERROR) << "Error creating string table image.";
    return false;
  }
  string_table_builder_->Dump(image, image_size);'''
    repaired = '''  size_t image_size = string_table_builder_->BinarySize();
  // RIMES: the final allocation can relocate the mmap for small dictionaries.
  const auto metadata_offset = reinterpret_cast<char*>(metadata_) - address();
  const auto syllabary_offset = reinterpret_cast<char*>(syllabary_) - address();
  const auto index_offset = reinterpret_cast<char*>(index_) - address();
  char* image = Allocate<char>(image_size);
  if (!image) {
    LOG(ERROR) << "Error creating string table image.";
    return false;
  }
  metadata_ = Find<table::Metadata>(metadata_offset);
  syllabary_ = Find<table::Syllabary>(syllabary_offset);
  index_ = Find<table::Index>(index_offset);
  string_table_builder_->Dump(image, image_size);'''
    text = path.read_text()
    if repaired in text:
        return
    if text.count(original) != 1:
        raise RuntimeError('Pinned Rime table implementation changed; review the relocation repair')
    path.write_text(text.replace(original, repaired))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: repair-rime-table.py PINNED_RIME_SOURCE')
    repair_table_image_relocation(Path(sys.argv[1]))
