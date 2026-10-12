# Local translation resources

RIMES downloads the exact resources in `resources.lock.json` (SHA-256 verified)
on first use. Text is subsequently translated offline; no API key is required.
This is an optional CPU model runtime, not a Windows system translation API.
The first download is about 182 MiB. Models currently cover Chinese ↔ English.

- Python 3.12.10: Python Software Foundation License, https://www.python.org/downloads/release/python-31210/
- CTranslate2 4.6.0: MIT, https://github.com/OpenNMT/CTranslate2
- SentencePiece 0.2.0: Apache-2.0, https://github.com/google/sentencepiece
- NumPy 2.2.6: BSD-3-Clause and bundled notices, https://numpy.org/
- PyYAML 6.0.2: MIT, https://pyyaml.org/
- setuptools 80.9.0: MIT, https://github.com/pypa/setuptools
- Argos Translate zh_en/en_zh 1.9 models, derived from OPUS-MT (CC-BY 4.0).
  Authors: Jörg Tiedemann and Santhosh Thottingal. “OPUS-MT — Building open
  translation services for the World”, EAMT 2020, Lisbon, Portugal.
  Their model README/metadata are retained alongside the unmodified weights.
  Source: https://github.com/argosopentech/argospm-index
  License: https://creativecommons.org/licenses/by/4.0/

Original wheel license files and Python's LICENSE.txt are retained in the private
runtime directory. Removing a plugin does not delete downloaded language data.
