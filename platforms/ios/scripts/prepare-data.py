#!/usr/bin/env python3
"""Generate minimal iOS schemas and compile dictionaries on the build host."""
from pathlib import Path
import os,subprocess,json,shutil,hashlib
root=Path(__file__).resolve().parents[3]
base=root/'platforms/ios/Resources/EngineData'
base.mkdir(parents=True,exist_ok=True)
os.environ.setdefault('DEVELOPER_DIR','/Applications/Xcode.app/Contents/Developer')
encoding=root/'Vendor/ios-build/encoding.swift'
encoding.write_text((root/'Shared/Sources/RimesCore/ChordEncoding.swift').read_text()+'\nprint(String(data:try! JSONSerialization.data(withJSONObject:["algebra":ZiranmaShuangpin.syllableAlgebra,"preedit":ZiranmaShuangpin.preeditFormat]),encoding:.utf8)!)\n')
rules=json.loads(subprocess.check_output(['xcrun','swift',str(encoding)],text=True))
shutil.copy2(root/'rime-data/wubi86.dict.yaml',base/'wubi86.dict.yaml')
(base/'lua').mkdir(exist_ok=True)
shutil.copy2(root/'rime-data/lua/rimes_wubi_autocommit.lua',base/'lua/rimes_wubi_autocommit.lua')
(base/'default.yaml').write_text('config_version: "1.0"\nschema_list:\n  - schema: rimes_pinyin\n  - schema: rimes_pinyin9\n  - schema: rimes_ziranma\n  - schema: rimes_wubi\n  - schema: lingxi_wubi\nmenu:\n  page_size: 9\n')
# Keep exact alphabetic spellings alongside each numeric alias, so choosing a
# syllable can constrain one segment while the remainder still uses nine keys.
syllables=set()
for line in (base/'pinyin_simp.dict.yaml').read_text().splitlines():
    fields=line.split('\t')
    if len(fields)>1:
        syllables.update(s for s in fields[1].split() if s.isascii() and s.isalpha() and s.islower())
digit_map=str.maketrans('abcdefghijklmnopqrstuvwxyz','22233344455566677778889999')
nine_algebra=[f'derive/^{s}$/{s.translate(digit_map)}/' for s in sorted(syllables)]
(base/'nine-key-syllables.json').write_text(json.dumps(sorted(syllables))+'\n')
for mode in ['pinyin','pinyin9','ziranma']:
    double=mode=='ziranma'
    alg=rules['algebra'] if double else ['abbrev/^([a-z]).+$/$1/']
    if mode=='pinyin9': alg=nine_algebra
    preedit=rules['preedit'] if double else []
    schema=f'''# RIMES iOS original minimal schema (Apache-2.0). No desktop includes or Lua.
schema:
  schema_id: rimes_{mode}
  name: XSIME {mode}
  version: "0.1.0"
switches:
  - name: ascii_mode
    reset: 0
engine:
  processors: [ascii_composer, speller, punctuator, selector, navigator, express_editor]
  segmentors: [ascii_segmentor, abc_segmentor, punct_segmentor, fallback_segmentor]
  translators: [punct_translator, script_translator]
speller:
  alphabet: {'abcdefghijklmnopqrstuvwxyz23456789' if mode=='pinyin9' else 'abcdefghijklmnopqrstuvwxyz'}
  delimiter: " '"
  algebra: {json.dumps(alg,ensure_ascii=False)}
translator:
  dictionary: pinyin_simp
  prism: rimes_{mode}
  enable_user_dict: true
  enable_sentence: true
  enable_completion: true
  spelling_hints: {99 if mode=='pinyin9' else 0}
  always_show_comments: {'true' if mode=='pinyin9' else 'false'}
  preedit_format: {json.dumps(preedit,ensure_ascii=False)}
punctuator:
  half_shape:
    ',': '，'
    '.': '。'
    '?': '？'
    '!': '！'
    ':': '：'
    ';': '；'
'''
    (base/f'rimes_{mode}.schema.yaml').write_text(schema)
# Share the desktop Wubi behavior, preserving the mobile schema and userdb IDs.
# The mobile bundle already includes pinyin_simp and its rimes_pinyin prism.
for source, target in [('wubi86', 'rimes_wubi'), ('lingxi_wubi', 'lingxi_wubi')]:
    schema = (root/'rime-data'/f'{source}.schema.yaml').read_text()
    schema = schema.replace(f'schema_id: {source}', f'schema_id: {target}')
    schema = schema.replace('    - rime_ice', '    - rimes_pinyin')
    schema = schema.replace('dictionary: rime_ice', 'dictionary: pinyin_simp')
    schema = schema.replace('prism: rime_ice', 'prism: rimes_pinyin')
    if source == 'wubi86':
        schema = schema.replace('  dictionary: wubi86', '  dictionary: wubi86\n  prism: rimes_wubi')
    schema = schema.replace('  enable_charset_filter: true', '  enable_charset_filter: true\n  enable_user_dict: true')
    schema = schema.replace('punctuator:\n  import_preset: default', """punctuator:
  half_shape:
    ',': '，'
    '.': '。'
    '?': '？'
    '!': '！'
    ':': '：'
    ';': '；'""")
    # Mobile default.yaml has no desktop key_binder/recognizer presets.
    schema = schema.replace('  import_preset: default\n', '')
    (base/f'{target}.schema.yaml').write_text(schema)
user=root/'Vendor/ios-build/data-user';user.mkdir(exist_ok=True)
subprocess.run([str(root/'Vendor/ios-build/host/rime/bin/rime_deployer'),'--build',str(base),str(base),str(base/'build')],check=True)
required=['rimes_pinyin.schema.yaml','rimes_pinyin9.schema.yaml','rimes_pinyin9.prism.bin','rimes_ziranma.schema.yaml','rimes_wubi.schema.yaml','lingxi_wubi.schema.yaml','lingxi_wubi.prism.bin','wubi86.reverse.bin','pinyin_simp.table.bin','wubi86.table.bin']
for name in required:
    if not (base/'build'/name).is_file():raise RuntimeError('Missing compiled asset: '+name)
subprocess.run(['python3',str(root/'platforms/ios/scripts/build-associations.py'),str(base)],check=True)
files={str(p.relative_to(base)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(base.rglob('*')) if p.is_file() and p.name!='checksums.json' and '.userdb' not in str(p)}
(base/'checksums.json').write_text(json.dumps(files,indent=2)+'\n')
print('Compiled',len(files),'resources')
