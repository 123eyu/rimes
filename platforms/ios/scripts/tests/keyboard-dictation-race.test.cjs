// Exercise the actual inline capture script with controllable asynchronous media.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../../Keyboard/KeyboardDictation.swift'), 'utf8');
const match = source.match(/<script>([\s\S]*?)<\/script>/);
assert.ok(match, 'keyboard capture script exists');

function harness({ suspended = false } = {}) {
  const requests = [], contexts = [], events = [];
  const navigator = {
    audioSession: { type: 'auto' },
    mediaDevices: { getUserMedia: () => new Promise((resolve, reject) => requests.push({ resolve, reject })) },
  };
  class AudioContext {
    constructor() {
      this.state = suspended ? 'suspended' : 'running';
      this.sampleRate = 48000;
      this.destination = {};
      this.closed = 0;
      contexts.push(this);
    }
    resume() { return new Promise(resolve => { this.resumeDone = resolve; }); }
    close() { this.closed++; return Promise.resolve(); }
    createMediaStreamSource() { return { connect() {}, disconnect() {} }; }
    createScriptProcessor() { return { connect() {}, disconnect() {} }; }
  }
  const window = {
    webkit: { messageHandlers: { capture: { postMessage: event => events.push(event.type) } } },
    addEventListener() {},
  };
  const context = vm.createContext({ window, navigator, AudioContext, Uint8Array, Float32Array });
  vm.runInContext(match[1], context);
  return { requests, contexts, events, navigator, run: code => vm.runInContext(code, context) };
}

function media() {
  const track = { stopped: 0, onended: null, stop() { this.stopped++; } };
  return { track, stream: { getTracks: () => [track], getAudioTracks: () => [track] } };
}

async function main() {
  {
    const h = harness(), m = media();
    const pending = h.run('startCapture()');
    h.run('stopCapture()');
    h.requests[0].resolve(m.stream);
    await pending;
    assert.equal(m.track.stopped, 1, 'a stream returned after stop is closed');
    assert.equal(h.contexts.length, 0, 'a late stream never creates an audio graph');
    assert.equal(h.navigator.audioSession.type, 'auto');
    assert.deepEqual(h.events, ['loaded']);
  }
  {
    const h = harness({ suspended: true }), m = media();
    const pending = h.run('startCapture()');
    h.requests[0].resolve(m.stream);
    // The capture script runs in a separate VM realm; let its queued promise
    // reactions reach the suspended audio context before requesting stop.
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(h.contexts.length, 1);
    h.run('stopCapture()');
    h.contexts[0].resumeDone();
    await pending;
    assert.equal(m.track.stopped, 1, 'stop closes a stream while audio is resuming');
    assert.equal(h.contexts[0].closed, 1);
    assert.deepEqual(h.events, ['loaded']);
  }
  {
    const h = harness(), old = media(), current = media();
    const first = h.run('startCapture()');
    h.run('stopCapture()');
    const second = h.run('startCapture()');
    h.requests[1].resolve(current.stream);
    await second;
    h.requests[0].resolve(old.stream);
    await first;
    assert.equal(old.track.stopped, 1, 'an obsolete request closes only its own stream');
    assert.equal(current.track.stopped, 0, 'a newer recording remains active');
    assert.deepEqual(h.events, ['loaded', 'started']);
    h.run('stopCapture()');
    assert.equal(current.track.stopped, 1);
  }
  {
    const h = harness(), first = media(), second = media();
    const a = h.run('startCapture()'); h.requests[0].resolve(first.stream); await a;
    const oldEnded = first.track.onended;
    h.run('stopCapture()');
    const b = h.run('startCapture()'); h.requests[1].resolve(second.stream); await b;
    oldEnded();
    assert.equal(second.track.stopped, 0, 'an old track ending does not stop a new recording');
    assert.deepEqual(h.events, ['loaded', 'started', 'started']);
    second.track.onended();
    assert.equal(second.track.stopped, 1);
    assert.deepEqual(h.events, ['loaded', 'started', 'started', 'error']);
  }
  console.log('Keyboard microphone cancellation races passed');
}

main().catch(error => { console.error(error); process.exitCode = 1; });
