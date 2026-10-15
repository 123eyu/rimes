# Android nine-key resources

`rimes_pinyin9.schema.yaml` and `nine-key-syllables.json` carry the XSIME mobile
nine-key Pinyin design used by the iOS keyboard (October 2, 2026). These are
Android-owned copies so this branch can build without updating or generating
anything in the iOS working tree. The minimal schema is original XSIME MIT code.

The schema derives telephone digits from each supported syllable, keeps the
existing `pinyin_simp` dictionary and user dictionary, and uses its own compiled
prism. Android's builder creates a no-user-dictionary variant and precompiles
both. No source dictionary, compiler or downloaded resource is used on the phone.

The syllable table drives explicit spelling choices in `NineKeyPinyin`.
`verify-engine.py` checks every table entry against the schema's digit rule,
compiled normal/private schemes, manifest hashes and existing dependency licenses.
The build receipt hashes both inputs. Source dictionary provenance and licenses
remain those in the pinned iOS dependency lock and `Licenses` directory.
