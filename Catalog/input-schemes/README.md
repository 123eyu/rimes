# Input scheme packages

This directory is the source of truth for the Chinese input schemes RIMES can offer for
import: where each one comes from, under what license, and whether the package built from it
passes the importer. The official website's scheme list is meant to be generated from
[`index.json`](index.json).

| File | Role | Edited by |
|---|---|---|
| [`sources.json`](sources.json) | Curated upstream sources, pinned to a commit | hand, or `scheme_build.py pin` |
| [`index.json`](index.json) | One record per package: status, checksum, schemes, license | `scheme_build.py index` |
| [`CATALOG.md`](CATALOG.md) | The same records as a table, by category | `scheme_build.py index` |
| `build/input-schemes/packages/*.zip` | The packages themselves (not committed) | `scheme_build.py build` |

The tooling lives in [`scripts/input-schemes/`](../../scripts/input-schemes).

## How RIMES imports a scheme

Only the iOS app imports third-party schemes today. Its importer is the contract every
package is built against.

| Platform | Import path | State |
|---|---|---|
| iOS | Settings › “Rime 方案包与导入”: pick a ZIP, review, deploy, test, enable | The contract. [`RimeSchemeImportService.swift`](../../platforms/ios/App/RimeSchemeImportService.swift) |
| macOS | None in the app. The F4 scheme list is reset to the five built-in schemes at startup | Packages are not consumed |
| Android | None. Setup states that external scheme import is not available yet | Packages are not consumed |
| Windows, Linux | No import in the app; both ship the built-in data set | Packages are not consumed |

A package is a plain Rime data directory in a ZIP, so the same file will serve the other
platforms when they gain an import path. Nothing in it is iOS-specific.

The importer deploys a package as **both** the shared and the user data directory, with only
the `default`, `lua` and `deployer` modules. Two consequences shape the format:

- A package must be self-contained. Presets (`default`, `key_bindings`, `punctuation`,
  `symbols`), dependency schemes and their dictionaries, the `essay` vocabulary, and OpenCC
  configurations with their dictionaries all have to be inside it. The iOS engine ships no
  OpenCC data of its own.
- There is no octagram or predict plugin. Language models (`*.gram`) are not carried, and
  Lua runs in a restricted runtime with a per-call budget.

## Package format, version 1

```
rimes-scheme-<id>-<yyyymmdd>.<commit7>.zip
├── <schema_id>.schema.yaml        1–32, all at the archive root
├── *.dict.yaml                    at the root; no subdirectories
├── default.yaml                   schema_list = the recommended schemes of this package
├── key_bindings.yaml  punctuation.yaml  symbols.yaml  …  presets the schemes reference
├── essay.txt  custom_phrase.txt  …                     text tables the schemes reference
├── rime.lua  lua/**               .lua, .txt, .json
├── opencc/**                      .json, .txt, .ocd, .ocd2, .lua
├── LICENSE-<owner>-<repo>.txt     one per upstream component that has a license file
└── rimes-package.yaml             manifest: id, version, components, licenses, modifications
```

`rimes-package.yaml` records, for every file, which upstream repository and commit it came
from and under which license, and lists every change made to an upstream file. The importer
does not read it yet; it is there for people and for a future “about this package” screen.

Limits, all enforced by the importer and by `scheme_lint.py`:

| Limit | Value |
|---|---|
| Archive size | 64 MB, no ZIP64, no encryption, no symbolic links |
| One file, expanded | 32 MB, and at most 100× its compressed size |
| All files, expanded | 128 MB |
| Entries | 512 |
| Schemes | 32, in one directory |
| One configuration file (`*.yaml` other than dictionaries) | 512 KB, one document, depth 64, 40,000 nodes |
| One Lua file | 1 MB, UTF-8 |
| Deployed data (sources plus compiled tables) | 512 MB |

Never in a package: `default.custom.yaml`, `installation.yaml`, `user.yaml`, other
frontends' settings (`squirrel`, `weasel`, `hamster`, `trime`, `ibus_rime`), `build/`,
`sync/`, `*.userdb*`. The importer drops them anyway.

Because `default.yaml` is part of every package, do not unzip one over an existing desktop
Rime user directory: it would replace that directory's own `default.yaml`.

## What the importer rejects that Rime accepts

Running the importer over real upstream repositories shows it is stricter than librime in
ten places. Stock `luna_pinyin`, `rime-ice` and `rime-combo-pinyin` are all rejected as
published. The builder edits the affected files so the package imports, and records each
edit in the manifest. Each of these could instead be relaxed in the importer; until then the
edits are what makes the catalog usable.

| Upstream construct | Importer | What the builder does |
|---|---|---|
| Optional reference to a file that is absent, `__patch: grammar:/hant?` (stock `luna_pinyin`) | Rejects the archive | Adds a placeholder file with the referenced node empty, so the patch is a no-op |
| Schemes that depend on each other (`luna_pinyin` ↔ `stroke`) | Staging fails | Removes the edge pointing back at a scheme that is deployed anyway |
| `__include: rime_ice.schema.yaml:/` (suffix spelled out) | Rejects the archive | Rewrites to `rime_ice.schema:/` |
| Local reference written `/split_canonicalize` or `:/key_binder/custom_bindings?` | Rejects the archive | Rewrites to the bare path |
| Resource ids in subdirectories, `import_tables: [cn_dicts/base]`, `user_dict: en_dicts/cn_en` | Rejects the archive | Moves the file to the root as `cn_dicts_base…` and rewrites the references |
| Duplicate keys in one mapping | Rejects the archive | Removes the earlier one when Rime's reading (last wins) is unchanged; leaves duplicated `__patch`/`__include` alone |
| Lua component `lua_x@*module*field` | Rejects the archive | Generates `lua/rimes_shims/…lua` returning that field and passes the original spelling as the name space |
| A dictionary over 32 MB | Rejects the archive | Splits it at a line boundary into `name.partN` tables and extends `import_tables` |
| An import table upstream leaves to the user (`*_custom`, `*user*`) | Blocks the scheme | Adds an empty table |
| `require` in a Lua block comment (the sample `rime.lua` of librime-lua) | Rejects the archive for a missing module | Prefixes those lines with `--` inside the comment |

One more is left alone because fixing it means editing executable code: the importer's
`require` scan also matches the word inside a string literal. `expe:find("require") or
expe:find("dofile")` in the current 星猫键道 calculator script is read as a module named
`) or expe:find(`, and the archive is rejected.

The table describes the importer whose SHA-256 is recorded as `verifiedWithImporter` in
`index.json`. When the importer changes, run `verify` again: it names every package on which
`scheme_lint.py` and the importer disagree, and the lint is then the one to correct.

What packaging cannot fix is reported as a failed status instead: malformed YAML, a required
dictionary or Lua module that upstream does not publish, a Lua script that exceeds the
restricted runtime's budget, and anything that needs a custom librime build.

## Catalog

`sources.json` lists one entry per package: `id`, names, `category`/`family`, the upstream
`repo` with a pinned `revision`, an optional `root` directory, and optionally the `schemas`
to recommend (default: every scheme in the root). `providers` are the shared sources that
complete a package: prelude, essay, the stock Rime schemes that others depend on, OpenCC
data, and 雾凇 for the files derived configurations borrow from it.

An entry may use `sourceArchive` (`url`, `sha256`, optional `licenseFile`) when a
formal release contains generated resources absent from the Git source archive.
The builder verifies the release checksum; `licenseFile` is fetched from the pinned
source commit if the ZIP omits it. `pin` leaves these entries fixed: update the
revision, release URL and checksum together. The manifest retains the archive source.

Wanxiang uses the v18.1.1 Base release and the explicit `wanxiang-portable-v18.1.1`
compatibility profile. It repairs the Lua cache database names, includes the release's
OpenCC data, and disables unavailable octagram, precompiled charset filtering and
desktop layout-switch/redeploy commands. It is a fixed full-pinyin package. See
[`wanxiang-portable.txt`](wanxiang-portable.txt) for the user-facing import instructions and limits.

Categories: `phonetic` 音码 (全拼, 双拼, 注音, 拼式, 并击), `shape` 形码 (五笔, 仓颉, 郑码,
笔画, 行列, 宇浩, 虎码, …), `phonetic-shape` 音形码 (小鹤音形, 自然码, 键道, 声笔, 魔然, 墨奇,
…), `dialect` 方言与古汉语, `auxiliary`.

`redistribution` decides whether the website may host the ZIP:

| Value | Meaning | Website |
|---|---|---|
| `open` | Upstream declares a license that permits redistribution with changes | May host the package |
| `restricted` | License forbids commercial use or derivatives (CC BY-NC-SA, CC BY-NC-ND) | Link to upstream; decide case by case |
| `undeclared` | Upstream declares no license | Link to upstream; ask the author before hosting |
| `proprietary` | Commercial code table | Link only |

It defaults from the license GitHub detects; set it explicitly in `sources.json` when the
LICENSE file says something GitHub cannot classify. A package is `publishable` in
`index.json` only when it is `open`, `verified`, and not graded `high` for device risk.

Package `status`:

| Status | Checked by | Meaning |
|---|---|---|
| `verified` | importer + engine | The real importer accepts and stages it; the iOS engine build deploys it, loads every recommended scheme without a Lua error, and any typing probes match |
| `importer-ok` | importer | As above, without the engine step (engine host slice not built) |
| `lint-ok` | `scheme_lint.py` | Passes the restated contract only |
| `import-fails` | importer or engine | Built, but rejected at inspect, stage, deploy, load or a probe; see `statusReason` |
| `partial`, `blocked` | lint | Some or all recommended schemes cannot be selected |
| `rejected` | lint | The importer would refuse the archive |
| `failed` | build | Could not be assembled from the source |

## Workflow

```bash
python3 -m venv .venv && .venv/bin/pip install -r scripts/input-schemes/requirements.txt
```

```bash
.venv/bin/python scripts/input-schemes/scheme_build.py build
```

```bash
.venv/bin/python scripts/input-schemes/scheme_build.py verify
```

```bash
.venv/bin/python scripts/input-schemes/scheme_build.py index
```

- `build` fetches each pinned revision into `build/input-schemes/cache`, assembles the
  package and writes `packages/*.zip` and `reports/<id>.json`. `--only id …` limits the run.
- `verify` compiles the unmodified `RimeSchemeImportService.swift` into a host command and
  runs `inspect` and `stage` on every package; then deploys and loads the staged result with
  [`host_probe.cc`](../../scripts/input-schemes/host_probe.cc), which repeats what
  `RimeMobile.deployResources` and `validateResources` do. It needs Swift, and for the engine
  step the host slice from `platforms/ios/scripts/build-engine.py host`. `--importer-ref HEAD`
  takes the importer from a commit instead of the working tree. A rebuild keeps the
  verification of a package whose content did not change; `verify --missing` checks only the
  rest. One engine step may take `--timeout` seconds (default 600).
- Packages that convert between simplified and traditional characters take their OpenCC
  data from `Vendor/rime/SharedSupport/opencc`, which `scripts/fetch-rime.sh` provides.
- `index` writes `index.json` and `CATALOG.md`.
- `scheme_lint.py <zip> …` checks any ZIP, including one a user sends in, against the contract.
- `pin --only id …` moves entries to the current upstream head. Rebuild and verify after.

To add a scheme, append an entry to `sources.json` (run `pin` to fill in the revision),
build and verify it, and commit `sources.json`, `index.json` and `CATALOG.md`. Add `probes`
(`schema`, `keys`, `expect`) when you know a code that must produce a given candidate.
Probes may set an `options` list (for example `["s2tw"]`) before typing. Both matching
output and a clean Lua runtime are required; a candidate produced alongside a Lua
error is a failed probe.

To publish, run `index --collect <dir>`: it copies the `publishable` ZIPs and a `SHA256SUMS`
file into `<dir>`. Upload those as release assets and let the website read `index.json`:
`archive`, `bytes` and `sha256` identify the download, `schemes` lists what the user will see
in the import review, `engine` carries the measured deployment cost. `build/input-schemes/packages`
also holds packages that are not publishable; do not upload that directory as a whole.

## What verification does and does not show

`verified` means the importer's own code accepted the package and the same engine build the
iOS app links (librime with the restricted Lua runtime, host slice) compiled and loaded it
on a Mac. It does not show behaviour on a phone: deployment there is slower and memory is
tighter. `engine.deploySeconds` and `engine.peakMemoryMB` are the Mac figures for compiling
the dictionaries, and `engine.deviceRisk` grades them against the one package known to deploy
on an iPhone 15 Pro (13 s and 1.2 GB on an M4, see
[the iOS validation record](../../platforms/ios/validation/rime-schemes-20261002.md)):
`medium` from 10 s or 1 GB, `high` from 60 s or 2 GB. The grades are a guide to what to try
on a device first, not a measurement. `high` packages are not `publishable`. Chord schemes load but need
key-release events the iOS keyboard does not send to imported schemes. Typing probes exist
for a small set of well-known schemes; for the rest, “loads” is the evidence.

## Licenses

Every package carries the license text of each component and names the component of every
file in `rimes-package.yaml`. Modified upstream files are listed there with the change, as
GPL and LGPL sources require. The packages are data under their upstream licenses; the
Apache-2.0 license of RIMES covers only the generated manifest, placeholders and shims.
Whether a code table itself may be licensed by the repository that publishes it (五笔, 郑码,
小鹤音形, 虎码, 嘸蝦米 and other schemes have rights holders) is not something this catalog can
settle: `open` records what upstream declares.
