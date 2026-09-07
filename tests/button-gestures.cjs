const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const source = fs.readFileSync(path.join(__dirname, '../Fragments_NNA/Fragments_NNA.ino'), 'utf8');
const start = source.indexOf('bool     btnRaw');
const end = source.indexOf('/* ===========================================================================\n *  CONTROL, OVER USB SERIAL');
assert.ok(start >= 0 && end > start, 'Firmware button-handler boundaries not found');
const button = source.slice(start, end);
const config = ['COMBO_MS', 'BTN_DEBOUNCE_MS', 'BTN_LONG_MS', 'BRIGHT_RAMP_MS', 'START_BRIGHT', 'BRIGHT_MAX']
  .map(key => {
    const match = source.match(new RegExp('^#define +'+ key +' +\\d+', 'm'));
    assert.ok(match, `Missing firmware constant: ${key}`);
    return match[0];
  }).join('\n');

// Exercise production debounce and gesture logic with a millisecond clock and stubbed I/O.
const harness = `#include <cstdint>
#include <cassert>
#include <cstdio>
${config}
#define BUTTON_PIN 12
#define BUTTON_ACTIVE_HIGH 1
#define HIGH 1
#define LOW 0
uint32_t gNow = 0;
uint8_t gMode = 5, gBright = 45;
int8_t gBrightDir = -1;
uint16_t gHue = 1000;
bool raw = false, gPlaying = true, dirty = false;
int digitalRead(int) { return raw; }
void settingsTouch() { dirty = true; }
void setMode(uint8_t m) { gMode = m; }
void rampHue() { gHue += 900; }
void rampBrightness() { gBright += gBrightDir; }
struct Logger { template<class... T> void printf(const char*, T...) {} } Serial;
${button}
void advance(int ms) { for (int i = 0; i < ms; i++) { gNow++; serviceButton(); } }
void hold(int ms) { raw = true; advance(ms); raw = false; advance(40); }
void reset() {
  raw = btnRaw = btnStable = btnLongFired = btnCombo = false;
  btnLongCount = 0; gMode = 5; gBright = 45; gBrightDir = -1; gHue = 1000;
  gPlaying = true; dirty = false; advance(1300);
}
int main() {
  reset(); gPlaying = false; raw = true; advance(26);
  assert(gPlaying && dirty && gMode == 5);
  raw = false; advance(40); assert(gMode == 6);

  reset(); gPlaying = false; hold(820);
  assert(gPlaying && gMode == 5 && gBright < 45);

  reset(); hold(820);
  assert(btnLongCount == 1 && gBright < 45 && gHue == 1000);
  advance(150); hold(820); assert(btnLongCount == 2 && gHue == 1000);
  advance(150); hold(900);
  assert(gHue > 1000 && gBright == 45 && gBrightDir == -1 && gMode == 5 && btnLongCount == 0);

  reset(); hold(820); advance(1300); hold(820);
  assert(btnLongCount == 1 && gHue == 1000);

  reset(); hold(820); hold(820); hold(100);
  assert(btnLongCount == 0 && gMode == 6);
  hold(820); assert(gHue == 1000);

  reset(); hold(15); assert(gMode == 5 && btnLongCount == 0);
  reset(); hold(699); assert(gMode == 6 && btnLongCount == 0 && gHue == 1000);
  puts("PASS: resume, colour combo, brightness restore, timeout, short-press reset, debounce, threshold");
}`;

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'fragments-button-test-'));
try {
  const cpp = path.join(tmp, 'button.cpp');
  const binary = path.join(tmp, 'button-test');
  fs.writeFileSync(cpp, harness);
  execFileSync('g++', ['-std=c++17', cpp, '-o', binary], { stdio: 'inherit' });
  execFileSync(binary, [], { stdio: 'inherit' });
} finally {
  fs.rmSync(tmp, { recursive: true, force: true });
}
