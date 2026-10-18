#!/usr/bin/python3
"""Isolated public CLI peer, never a provider or personal credential reader."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

os.umask(0o077)
home = Path(os.environ['HOME'])
mode = (home / 'fixture-mode').read_text()
assert all(key not in os.environ for key in ('ANTHROPIC_API_KEY', 'ANTHROPIC_BASE_URL', 'CLAUDE_CODE_OAUTH_TOKEN',
                                           'CLAUDE_CONFIG_DIR', 'NODE_OPTIONS', 'PYTHONPATH', 'CODEX_HOME'))
assert os.getcwd() == os.environ['TMPDIR']
args = sys.argv[1:]
required = ['--print', '--verbose', '--output-format', '--include-partial-messages', '--tools',
            '--disable-slash-commands', '--safe-mode', '--strict-mcp-config', '--no-session-persistence',
            '--permission-mode', '--no-chrome']
if args == ['--help']:
    for option in required:
        if mode == 'unsupported' and option == '--safe-mode':
            print('  Safety prose mentions --safe-mode but does not declare it')
        else:
            print('  ' + option + '  Public fixture option')
    sys.exit(0)
if args == ['auth', 'status', '--json']:
    logged = mode not in ('logged-out', 'login', 'login-hold') or (home / 'logged-in').exists()
    print(json.dumps({'loggedIn': logged, 'email': 'PRIVATE-ACCOUNT-FIXTURE@example.invalid'}))
    sys.exit(0 if logged and mode != 'auth-exit-fail' else 1)
if args == ['auth', 'login', '--claudeai']:
    print('PRIVATE-AUTH-CODE-FIXTURE', flush=True)
    (home / 'login-started').write_text('public')
    if mode == 'login-hold':
        while True:
            time.sleep(.01)
    (home / 'logged-in').write_text('CLI-owned-public-fixture')
    sys.exit(0)
assert args[:4] == ['--print', '--verbose', '--output-format', 'stream-json']
for option in required:
    assert option in args
assert args[args.index('--tools') + 1] == ''
assert args[args.index('--permission-mode') + 1] == 'dontAsk'
assert args[-4:] == ['--model', 'claude-opus-5-5', '--effort', 'high']
prompt = sys.stdin.read()
assert '公开' in prompt and 'RIMES_SOURCE_TEXT' not in prompt
(home / 'inference-started').write_text('public')
if mode == 'oversized':
    os.write(1, b'x' * 800000)
    time.sleep(3)
    sys.exit(0)
if mode == 'child':
    child = subprocess.Popen(['/usr/bin/python3', '-c', 'import signal,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(30)'])
    (home / 'child-pid').write_text(str(child.pid))
    while True:
        time.sleep(.01)
if mode == 'timeout':
    time.sleep(10)
    sys.exit(0)

def send(value):
    data = (json.dumps(value, ensure_ascii=False) + '\n').encode()
    for i in range(0, len(data), 3 if mode == 'fragmented' else len(data)):
        os.write(1, data[i:i + (3 if mode == 'fragmented' else len(data))])

def event(kind, **fields):
    send({'type': 'stream_event', 'session_id': 'public-session', 'event': {'type': kind, **fields}})

send({'type': 'system', 'subtype': 'init', 'tools': ['Bash'] if mode == 'tools' else [],
      'mcp_servers': ['trap'] if mode == 'mcp' else [], 'session_id': 'public-session',
      'model': 'unexpected' if mode == 'model' else 'claude-opus-5-5', 'permissionMode': 'dontAsk'})
event('content_block_start', content_block={'type': 'thinking'})
event('content_block_delta', delta={'type': 'thinking_delta', 'thinking': 'PRIVATE-REASONING-FIXTURE'})
event('content_block_start', content_block={'type': 'tool_use' if mode == 'tool-block' else 'text'})
chunks = ['{"blocks":[{"text":"公开🙂。","title":null}', ',{"text":"第二句。","title":null}]}']
event('content_block_delta', delta={'type': 'text_delta', 'text': chunks[0]})
if mode == 'text-hold':
    (home / 'text-started').write_text('public')
    while not (home / 'release-text').exists():
        time.sleep(.01)
event('content_block_delta', delta={'type': 'text_delta', 'text': chunks[1]})
event('message_stop')
if mode == 'no-result':
    sys.exit(0)
if mode == 'duplicate-key':
    os.write(1, b'{"type":"result","type":"result"}\n')
    sys.exit(0)
send({'type': 'result', 'subtype': 'error' if mode == 'failed' else 'success', 'is_error': mode == 'failed',
      'session_id': 'another-session' if mode == 'session' else 'public-session', 'result': ''.join(chunks)})
sys.exit(4 if mode == 'exit-fail' else 0)
