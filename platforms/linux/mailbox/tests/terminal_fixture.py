#!/usr/bin/env python3
"""Public PTY peer, independent of real CLI accounts and personal files."""
import os
import signal
import sys
import termios
import time
import tty
from pathlib import Path

assert os.isatty(0) and os.isatty(1) and os.isatty(2), 'real PTY required'
assert os.getsid(0) == os.getpid(), 'independent PTY session required'
assert os.environ['TERM'] == 'xterm-256color'
assert 'RIMES_PRIVATE_TEST_SECRET' not in os.environ, 'ambient IME secret inherited'
tty.setraw(0)

def output(text):
    os.write(1, (text + '\r\n').encode())

def size(*_):
    geometry = os.get_terminal_size(0)
    output(f'SIZE:{geometry.columns}:{geometry.lines}')

signal.signal(signal.SIGWINCH, size)
output('READY:' + os.getcwd())
size()
buffer = b''
while True:
    raw = os.read(0, 4096)
    if not raw:
        break
    output('KEY:' + raw.hex())
    buffer += raw
    while b'\r' in buffer or b'\n' in buffer:
        split = min(i for i, value in enumerate(buffer) if value in (10, 13))
        command, buffer = buffer[:split], buffer[split+1:]
        if command == b'quit7':
            sys.exit(7)
        if command == b'where':
            output('CWD:' + os.getcwd())
        elif command == b'size':
            size()
        elif command == b'fork':
            child = os.fork()
            if not child:
                os.setpgid(0, 0)  # Different foreground/job-control process group.
                signal.signal(signal.SIGTERM, signal.SIG_IGN)
                Path('child.pid').write_text(str(os.getpid()))
                while True:
                    time.sleep(1)
            signal.signal(signal.SIGTERM, signal.SIG_IGN)
            output('CHILD:' + str(child))
        elif command == b'title':
            os.write(1, b'\x1b]0;identical-output.txt\x07')
        else:
            output('ECHO:' + command.decode('utf8', errors='replace'))
