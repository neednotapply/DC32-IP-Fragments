const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const source = fs.readFileSync(path.join(__dirname, '../DC32_Fragments.ino'), 'utf8');
const start = source.indexOf('bool     btnRaw');
const end = source.indexOf('/* ===========================================================================\n *  CONTROL, OVER USB SERIAL');
assert.ok(start >= 0 && end > start, 'Firmware button-handler boundaries not found');
const button = source.slice(start, end);
const menuStart = source.indexOf('enum ButtonMenu');
const menuEnd = source.indexOf('// While you are navigating', menuStart);
assert.ok(menuStart >= 0 && menuEnd > menuStart, 'Firmware menu boundaries not found');
const menu = source.slice(menuStart, menuEnd);
const config = ['MENU_IDLE_MS', 'BTN_DEBOUNCE_MS', 'BTN_LONG_MS', 'BRIGHT_RAMP_MS', 'BRIGHT_MAX']
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
enum Feedback { FB_NONE, FB_MODE, FB_LEVEL };
Feedback gFeedback = FB_NONE;
uint32_t gFeedbackTo = 0;
uint32_t gFrame = 0;
void fbClear() {}
${menu}
int digitalRead(int) { return raw; }
void settingsTouch() { dirty = true; }
void setMode(uint8_t m) { gMode = m; gButtonMenu = MENU_NONE; }
void rampHue() { gHue += 900; keepMenuOpen(); }
void rampBrightness() { if (gBright > 4) gBright += gBrightDir; keepMenuOpen(); }
struct Logger { template<class... T> void printf(const char*, T...) {} } Serial;
${button}
void advance(int ms) { for (int i = 0; i < ms; i++) { gNow++; serviceButton(); } }
void hold(int ms) { raw = true; advance(ms); raw = false; advance(40); }
void reset() {
  raw = btnRaw = btnStable = btnLongFired = false;
  gButtonMenu = MENU_NONE; gFeedback = FB_NONE; gFeedbackTo = 0;
  gMode = 5; gBright = 45; gBrightDir = -1; gHue = 1000;
  gPlaying = true; dirty = false; advance(MENU_IDLE_MS + 100);
}
int main() {
  reset(); gPlaying = false; raw = true; advance(26);
  assert(gPlaying && dirty && gMode == 5);
  raw = false; advance(40); assert(gMode == 6);

  reset(); gPlaying = false; hold(820);
  assert(gPlaying && gMode == 5 && gBright < 45);

  reset(); hold(820);
  assert(gButtonMenu == MENU_BRIGHTNESS && gFeedback == FB_LEVEL && gBright < 45 && gHue == 1000);
  uint8_t brightness = gBright;
  advance(150); hold(820);
  assert(gButtonMenu == MENU_COLOUR && gHue > 1000 && gBright == brightness);
  uint16_t hue = gHue;
  advance(150); hold(820);
  assert(gButtonMenu == MENU_BRIGHTNESS && gHue == hue && gBright < brightness && gMode == 5);
  hold(820); assert(gButtonMenu == MENU_COLOUR && gHue > hue);

  reset(); hold(820); advance(1300); hold(820);
  assert(gButtonMenu == MENU_COLOUR && gHue > 1000);

  reset(); hold(820); advance(MENU_IDLE_MS - 80);
  assert(gButtonMenu == MENU_BRIGHTNESS);
  raw = true; advance(600);
  assert(gButtonMenu == MENU_BRIGHTNESS && (int32_t)(gFeedbackTo - gNow) > 0);
  advance(220); raw = false; advance(40);
  assert(gButtonMenu == MENU_COLOUR && gHue > 1000);

  reset(); hold(820); advance(MENU_IDLE_MS + 1);
  assert(gButtonMenu == MENU_NONE);
  hold(820); assert(gButtonMenu == MENU_BRIGHTNESS && gHue == 1000);

  reset(); hold(MENU_IDLE_MS + 1500);
  assert(gButtonMenu == MENU_BRIGHTNESS && gHue == 1000);

  reset(); hold(820); hold(820); hold(100);
  assert(gButtonMenu == MENU_NONE && gFeedback == FB_NONE && gMode == 5);
  hue = gHue; hold(820); assert(gButtonMenu == MENU_BRIGHTNESS && gHue == hue);
  gPlaying = false; dirty = false; hold(100);
  assert(gButtonMenu == MENU_NONE && gMode == 5 && gPlaying && dirty);
  hold(100); assert(gMode == 6);

  reset(); hold(15); assert(gMode == 5 && gButtonMenu == MENU_NONE);
  reset(); hold(700); assert(gMode == 6 && gButtonMenu == MENU_NONE && gHue == 1000);
  reset(); hold(701); assert(gMode == 5 && gButtonMenu == MENU_BRIGHTNESS);

  reset(); gNow = UINT32_MAX - 1000;
  hold(820); advance(MENU_IDLE_MS - 80); hold(820);
  assert(gButtonMenu == MENU_COLOUR && gHue > 1000);
  advance(MENU_IDLE_MS + 1); assert(gButtonMenu == MENU_NONE);

  puts("PASS: resume, brightness/colour menus, retained settings, idle/held deadlines, debounce, threshold, clock rollover");
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
