#!/usr/bin/env python3
"""Loopback conversation peer. Exact models and credentials are public test values."""
import http.server
import json
import subprocess
import sys
import threading
import time

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_POST(self):
        try:
            length = int(self.headers['Content-Length'])
            assert 0 < length < 2 * 1048576
            request = json.loads(self.rfile.read(length))
            assert request['model'] == 'fixture-model'
            assert self.headers['Authorization'] == 'Bearer fixture-credential'
            assert self.path == '/chat/chat/completions'
            prompt = request['messages'][1]['content']
            if 'CONVERSATION_JSON:\n' in prompt:
                turns = json.loads(prompt.split('CONVERSATION_JSON:\n', 1)[1])
                assert [t['role'] for t in turns] == ['user', 'assistant', 'user']
                assert turns[1]['content'] == '公开🙂。\n\n第二句。'
            else:
                assert json.loads(prompt.split('USER_PAYLOAD_JSON:\n', 1)[1]) == {'mode': 'ask', 'format': 'plain', 'source': '公开 API 问题'}
            self.send_response(200)
            self.send_header('Content-Type', 'text/event-stream')
            self.end_headers()
            for text in ['{"blocks":[{"text":"公开🙂。","title":null}', ',{"text":"第二句。","title":null}]}']:
                event = {'choices': [{'index': 0, 'delta': {'content': text}, 'finish_reason': None}]}
                self.wfile.write(('data: ' + json.dumps(event, ensure_ascii=False) + '\n\n').encode())
                self.wfile.flush()
                time.sleep(.08)
            self.wfile.write(b'data: [DONE]\n\n')
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception as error:
            self.server.failures.append(type(error).__name__)

if __name__ == '__main__':
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    server.failures = []
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        result = subprocess.run(sys.argv[1:] + [f'http://127.0.0.1:{server.server_port}'], timeout=50)
        if server.failures:
            raise SystemExit('Conversation contract failures: ' + ', '.join(server.failures))
        raise SystemExit(result.returncode)
    finally:
        server.shutdown()
        server.server_close()
