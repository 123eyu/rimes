#!/usr/bin/python3
"""Public deterministic app-server peer. Never contacts a provider."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

home = Path(os.environ["CODEX_HOME"])
mode_file = home / "fixture-mode"
mode = mode_file.read_text() if mode_file.exists() else "text"
chat = mode.startswith("chat-")
if chat:
    mode = mode[5:]
assert all(name not in os.environ for name in ("OPENAI_API_KEY", "CODEX_API_KEY", "CODEX_BASE_URL", "NODE_OPTIONS", "PYTHONPATH"))
assert sys.argv[1] == "app-server" and "--strict-config" in sys.argv
assert sys.argv[-2:] == ["--listen", "stdio://"]
assert os.getcwd() == os.environ["TMPDIR"] + "/workspace"
assert 'forced_login_method = "chatgpt"' in (home / "config.toml").read_text()
if mode == "unsupported":
    sys.exit(2)

def send(value):
    data = (json.dumps(value, ensure_ascii=False) + "\n").encode()
    if mode == "fragmented":
        for offset in range(0, len(data), 3):
            os.write(1, data[offset:offset + 3])
    else:
        os.write(1, data)

def result(request, value):
    send({"id": request["id"], "result": value})

def event(method, value):
    send({"method": method, "params": value})

def save(name, value):
    path = home / name
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as stream:
        stream.write(value)

for line in sys.stdin:
    request = json.loads(line)
    method = request["method"]
    if method == "initialize":
        result(request, {"userAgent": "public-fixture"})
    elif method == "initialized":
        pass
    elif method == "mcpServerStatus/list":
        result(request, {"data": [{"name": "trap"}] if mode == "mcp" else [], "nextCursor": None})
    elif method == "account/read":
        result(request, {"account": None if mode == "logged-out" else {"type": "chatgpt"}, "requiresOpenaiAuth": True})
    elif method == "account/login/start":
        assert request["params"] == {"type": "chatgpt"}
        url = "https://auth.openai.com/public-fixture" if mode != "bad-login-url" else "https://auth.openai.com.evil.invalid/"
        result(request, {"type": "chatgpt", "loginId": "fixture-login", "authUrl": url})
        event("account/login/completed", {"loginId": "unrelated", "success": False})
        if mode not in ("login-cancel", "bad-login-url"):
            save("auth.json", json.dumps({"auth_mode": "chatgpt", "tokens": {"access_token": "public-fixture", "refresh_token": "public-fixture"}}))
            event("account/login/completed", {"loginId": "fixture-login", "success": True})
    elif method == "thread/start":
        p = request["params"]
        assert p["approvalPolicy"] == "never" and p["ephemeral"] and p["personality"] == "none"
        assert p["model"] == "gpt-6-sol" and p["allowProviderModelFallback"] is False
        assert p["config"]["model_reasoning_effort"] == ("medium" if chat else "high")
        result(request, {"thread": {"id": "fixture-thread"}, "model": "other" if mode == "model-mismatch" else "gpt-6-sol"})
    elif method == "turn/start":
        p = request["params"]
        assert p["threadId"] == "fixture-thread" and p["model"] == "gpt-6-sol" and p["effort"] == ("medium" if chat else "high")
        assert p["outputSchema"]["properties"]["blocks"]["maxItems"] == 20
        if chat:
            prompt = p["input"][0]["text"]
            assert "USER_PAYLOAD_JSON:\n" in prompt or "CONVERSATION_JSON:\n" in prompt
            save("last-chat-prompt", prompt)
        save("last-input.json", json.dumps(p, ensure_ascii=False))
        if mode == "malformed":
            os.write(1, b'{"id":4,"id":4,"result":{}}\n')
            continue
        result(request, {"turn": {"id": "fixture-turn"}})
        if mode == "tree-cancel":
            child = subprocess.Popen(["/usr/bin/python3", "-c", "import signal,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(30)"])
            save("child-pid", str(child.pid))
            time.sleep(30)
            continue
        if mode == "timeout":
            time.sleep(30)
            continue
        if mode == "oversized":
            os.write(1, b"x" * (2 * 1048576))
            continue
        if mode == "server-request":
            send({"id": "approval", "method": "item/commandExecution/requestApproval", "params": {}})
            continue
        if mode in ("image", "image-hold", "image-missing", "image-escape"):
            assert p["input"][0]["type"] == "skill" and p["input"][0]["name"] == "imagegen"
            assert Path(p["input"][0]["path"]).is_file()
            assert sys.argv[sys.argv.index("image_generation") - 1] == "--enable"
            event("item/started", {"item": {"type": "imageGeneration", "id": "image"}})
            if mode == "image-hold":
                save("image-started", "started")
                while not (home / "release-image").exists():
                    time.sleep(.01)
            if mode != "image-missing":
                image_root = home / "generated_images"
                image_root.mkdir(mode=0o700, exist_ok=True)
                # A real 1x1 PNG is used by the Mailbox ownership tests.
                import base64
                image = image_root / "public.png"
                image.write_bytes(base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+ip1sAAAAASUVORK5CYII="))
                image.chmod(0o600)
                event("item/completed", {"item": {"type": "imageGeneration", "id": "image", "status": "completed", "savedPath": str(image) if mode in ("image", "image-hold") else "/tmp/outside.png"}})
        else:
            assert all(item["type"] == "text" for item in p["input"])
            assert sys.argv[sys.argv.index("image_generation") - 1] == "--disable"
            event("item/agentMessage/delta", {"threadId": "unrelated", "itemId": "late", "delta": "NEVER DISPLAY"})
            event("item/reasoning/textDelta", {"delta": "NEVER DISPLAY"})
            event("item/started", {"item": {"type": "agentMessage", "id": "status", "phase": "commentary"}})
            event("item/agentMessage/delta", {"itemId": "status", "delta": "NEVER DISPLAY"})
            event("item/started", {"item": {"type": "agentMessage", "id": "answer", "phase": "final_answer"}})
            fragments = ['{"blocks":[{"text":"公开🙂。","title":null}', ',{"text":"第二句。","title":null}]}']
            for index, fragment in enumerate(fragments):
                event("item/agentMessage/delta", {"itemId": "answer", "delta": fragment})
                if index == 0 and mode == "text-hold":
                    save("text-started", "started")
                    while not (home / "release-text").exists():
                        time.sleep(.01)
            event("item/completed", {"item": {"type": "agentMessage", "id": "answer", "phase": "final_answer", "text": "".join(fragments)}})
        event("turn/completed", {"turn": {"id": "fixture-turn", "status": "interrupted" if mode == "interrupted" else "completed"}})
    elif method in ("turn/interrupt", "account/login/cancel"):
        break
    else:
        raise AssertionError(method)
