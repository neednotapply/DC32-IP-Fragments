const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const { execFileSync } = require('node:child_process');

const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'studio.html'), 'utf8');
const source = fs.readFileSync(path.join(root, 'DC32_Fragments.ino'), 'utf8');
const animationScript = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)][0][1];
const context = vm.createContext({ assert });
vm.runInContext(animationScript + '\nconst EYE_FLOOR = 95;', context);
vm.runInContext(`
updateInk();
const rainHeadLevel = 220;
function rgb(i) { return Array.from(fb.slice(i * 3, i * 3 + 3)); }
function litRing() { return Array.from({length:RING}, (_,i)=>i).filter(i=>rgb(i).some(Boolean)); }
function resetAnimation() {
  fbClear(); scratch.fill(0); gFrame=0; now=12345;
  for (const k in st) delete st[k];
}
resetAnimation(); animBoot();
assert.equal(JSON.stringify(litRing()), '[43]');
gFrame++; now+=34; animBoot();
assert.ok(rgb(44).some(Boolean));
resetAnimation(); animAperture();
assert.equal(JSON.stringify(litRing()), '[0,20,40]');
gFrame++; now+=600; animAperture(); assert.equal(litRing().length,60);
now+=2800; animAperture(); assert.equal(JSON.stringify(litRing()), '[0,20,40]');

const order = ANIMS.slice(9).map(a=>a.name);
assert.equal(JSON.stringify(order), ' ["Rainbow","Drift","Plasma","Collide"]'.trim());
for (const time of [0,1000,3276]) {
  now=time; animRainbow();
  for (const eye of [EYE_L,EYE_R]) {
    let nearest=0, best=Infinity;
    for(let p=0;p<PERIM_COUNT;p++) {
      const dx=pxX(eye)-pxX(PERIM[p]), dy=pxY(eye)-pxY(PERIM[p]);
      const d=dx*dx+3*dy*dy;
      if(d<best) {best=d;nearest=p;}
    }
    const colour=ColorHSV(u16(time*14-nearest*Math.floor(65536/PERIM_COUNT)),255,170);
    assert.equal(JSON.stringify(rgb(eye)),JSON.stringify([(colour>>>16)&255,(colour>>>8)&255,colour&255]));
    assert.equal(Math.sign(pxX(PERIM[nearest])), Math.sign(pxX(eye)));
  }
}

const levels=Array.from({length:5},()=>[]);
for(let height=-25;height<=125;height++) {
  for(let i=0;i<N;i++) for(let j=i+1;j<N;j++) {
    if(pxY(i)===pxY(j)) assert.equal(scannerBeam(i,height),scannerBeam(j,height));
  }
}
for(let t=0;t<2304;t+=9) {
  now=t;animScanner();
  for(let i=60;i<65;i++) levels[i-60].push(Math.max(...rgb(i)));
  assert.deepEqual(rgb(EYE_L),rgb(EYE_R));
}
for(const values of levels) assert.ok(Math.max(...values)-Math.min(...values)>120);

for(let seed=0;seed<RING;seed++) {
  let head=rainNext(255,pxX(seed)), previousY=101, steps=0;
  while(head!==255) {
    assert.ok(pxY(head)<previousY);
    previousY=pxY(head);head=rainNext(head,pxX(seed));
    assert.ok(++steps<=RING);
  }
}
resetAnimation();
let previous=null, previousLevels=new Array(RING).fill(0), transitions=0;
for(let frame=0;frame<600;frame++) {
  gFrame=frame;now=frame*30;animMatrix();
  const arrivals=new Set();
  for(let d=0;d<DROPS;d++) {
    const head=st.head[d];
    if(head===255) continue;
    if(!previous || head!==previous[d]) {
      arrivals.add(head);
      assert.equal(scratch[head], rainHeadLevel);
      assert.ok(Math.max(...rgb(head)) < 255);
    }
    if(previous && previous[d]!==255 && head!==previous[d]) {
      assert.ok(pxY(head)<pxY(previous[d]));transitions++;
    }
  }
  for(let i=0;i<RING;i++) {
    assert.equal(scratch[i],arrivals.has(i)?rainHeadLevel:scale8(previousLevels[i],TRAIL_DECAY));
    assert.equal(Math.max(...rgb(i)),scratch[i]);
  }
  previous=st.head.slice();
  previousLevels=Array.from(scratch.slice(0,RING));
}
assert.ok(transitions>100);
for(const hue of [0,10000,18900,37847,50000]) {
  gHue=hue;updateInk();scratch[0]=rainHeadLevel;rainPixel(0);
  assert.ok(Math.max(...rgb(0)) < 255);
  let prior=rgb(0);
  for(let frame=0;frame<120;frame++) {
    scratch[0]=scale8(scratch[0],TRAIL_DECAY);rainPixel(0);
    const current=rgb(0);
    assert.ok(current.every((value,c)=>value<=prior[c]));prior=current;
  }
  assert.equal(rgb(0).join(),'0,0,0');
  const ink=[inkR,inkG,inkB], error=level=>{
    scratch[0]=level;rainPixel(0);
    return rgb(0).reduce((sum,v,c)=>sum+Math.abs(v/level-ink[c]/255),0);
  };
  assert.ok(error(64)<error(200),'Trail did not approach selected custom color');
  scratch[0]=rainHeadLevel;rainPixel(0);assert.ok(Math.max(...rgb(0)) < 255);
}
`, context);

// Compile the actual firmware animation code with only the Arduino I/O stubbed out.
const begin = source.indexOf('// Configuration --');
const end = source.indexOf('\nPreferences prefs;');
assert.ok(begin >= 0 && end > begin);
const firmware = source.slice(begin, end);
const harness = `#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cassert>
#include <algorithm>
#define PI 3.14159265358979323846
#define NEO_RGB 0
#define NEO_KHZ800 0
uint32_t rng=1;
long random(long n) { rng=rng*1664525U+1013904223U; return (long)((double)rng/4294967296.0*n); }
long random(long lo,long hi) {return lo+random(hi-lo);}
struct Adafruit_NeoPixel {
  Adafruit_NeoPixel(int,int,int) {}
  void show() {}
  void setPixelColor(int,uint32_t) {}
  static uint32_t Color(uint8_t r,uint8_t g,uint8_t b) {return (uint32_t)r<<16|(uint32_t)g<<8|b;}
  static uint32_t ColorHSV(uint16_t h,uint8_t s=255,uint8_t v=255) {
    uint16_t hue=((uint32_t)h*1530+32768)/65536;
    uint8_t r,g,b;
    if(hue<510) {b=0;if(hue<255){r=255;g=hue;}else{r=510-hue;g=255;}}
    else if(hue<1020) {r=0;if(hue<765){g=255;b=hue-510;}else{g=1020-hue;b=255;}}
    else if(hue<1530) {g=0;if(hue<1275){r=hue-1020;b=255;}else{r=255;b=1530-hue;}}
    else {r=255;g=0;b=0;}
    uint32_t v1=1+v,s1=1+s,s2=255-s;
    return ((((((r*s1)>>8)+s2)*v1)&0xff00)<<8)|(((((g*s1)>>8)+s2)*v1)&0xff00)|(((((b*s1)>>8)+s2)*v1)>>8);
  }
};
${firmware}
bool lit(int i) { return fb[i][0] || fb[i][1] || fb[i][2]; }
int peak(int i) { return std::max({fb[i][0],fb[i][1],fb[i][2]}); }
int countRing() {int n=0;for(int i=0;i<60;i++)if(lit(i))n++;return n;}
int main() {
  buildGeometry(); updateInk();
  gFrame=0;gAnimNow=12345;animBoot();assert(countRing()==1 && lit(43));
  gFrame++;gAnimNow+=34;animBoot();assert(lit(44));
  gFrame=0;gAnimNow=98765;animAperture();assert(countRing()==3 && lit(0)&&lit(20)&&lit(40));
  gFrame++;gAnimNow+=600;animAperture();assert(countRing()==60);
  gAnimNow+=2800;animAperture();assert(countRing()==3 && lit(0)&&lit(20)&&lit(40));
  for(uint32_t time:{0U,1000U,3276U}) {
    gAnimNow=time;animRainbow();
    for(uint8_t eye:{EYE_L,EYE_R}) {
      uint32_t best=UINT32_MAX;uint8_t nearest=0;
      for(int p=0;p<PERIM_COUNT;p++) {
        int dx=pxX(eye)-pxX(PERIM[p]),dy=pxY(eye)-pxY(PERIM[p]);
        uint32_t d=dx*dx+3*dy*dy;if(d<best){best=d;nearest=p;}
      }
      uint32_t colour=strip.ColorHSV((uint16_t)(time*14-nearest*(65536UL/PERIM_COUNT)),255,170);
      assert(strip.Color(fb[eye][0],fb[eye][1],fb[eye][2])==colour);
      assert(pxX(eye)*pxX(PERIM[nearest])>0);
    }
  }
  for(int height=-25;height<=125;height++) {
    for(int i=0;i<PIXEL_COUNT;i++)for(int j=i+1;j<PIXEL_COUNT;j++)
      if(pxY(i)==pxY(j))assert(scannerBeam(i,height)==scannerBeam(j,height));
  }
  int low[5]={255,255,255,255,255},high[5]={};
  for(int t=0;t<2304;t+=9) {
    gAnimNow=t;animScanner();
    for(int i=60;i<65;i++){low[i-60]=std::min(low[i-60],peak(i));high[i-60]=std::max(high[i-60],peak(i));}
    assert(!memcmp(fb[EYE_L],fb[EYE_R],3));
  }
  for(int i=0;i<5;i++)assert(high[i]-low[i]>120);
  for(int seed=0;seed<60;seed++) {
    uint8_t head=rainNext(255,pxX(seed));int y=101,steps=0;
    while(head!=255){assert(pxY(head)<y);y=pxY(head);head=rainNext(head,pxX(seed));assert(++steps<=60);}
  }
  int whiteFrames=0;
  for(int f=0;f<600;f++) {
    gFrame=f;gAnimNow=f*30;animMatrix();
    int heads=0;for(int i=0;i<60;i++)if(scratch[i]==RAIN_HEAD_LEVEL)heads++;
    assert(heads<=DROPS);if(heads)whiteFrames++;
  }
  assert(whiteFrames>150);
  for(uint16_t hue:{0,10000,18900,37847,50000}) {
    gHue=hue;updateInk();scratch[0]=RAIN_HEAD_LEVEL;rainPixel(0);
    assert(std::max({fb[0][0],fb[0][1],fb[0][2]})<255);
    uint8_t prior[3];memcpy(prior,fb[0],3);
    for(int frame=0;frame<120;frame++) {
      scratch[0]=scale8(scratch[0],TRAIL_DECAY);rainPixel(0);
      for(int c=0;c<3;c++){assert(fb[0][c]<=prior[c]);prior[c]=fb[0][c];}
    }
    assert(!lit(0));
    auto error=[&](uint8_t level){
      scratch[0]=level;rainPixel(0);uint8_t ink[]={inkR,inkG,inkB};float sum=0;
      for(int c=0;c<3;c++)sum+=fabsf((float)fb[0][c]/level-(float)ink[c]/255);
      return sum;
    };
    assert(error(64)<error(200));
    scratch[0]=RAIN_HEAD_LEVEL;rainPixel(0);assert(std::max({fb[0][0],fb[0][1],fb[0][2]})<255);
  }
  const char* names[]={"Rainbow","Drift","Plasma","Collide"};
  for(int i=0;i<4;i++)assert(!strcmp(ANIMS[9+i].name,names[i]));
  for(uint8_t m=0;m<13;m++) {
    assert(storedMode(m,1)==(m==12?9:(m>=9?m+1:m)));
    assert(storedMode(m,2)==m);
  }
  puts("PASS: firmware/simulator starts, eye sampling, scan, downward rain, white-to-custom-color fade, refresh, order and migration");
}`;
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'fragments-animations-'));
try {
  const cpp = path.join(tmp, 'animations.cpp'), binary = path.join(tmp, 'animations');
  fs.writeFileSync(cpp, harness);
  execFileSync('g++', ['-std=c++17', cpp, '-o', binary], { stdio: 'inherit' });
  execFileSync(binary, [], { stdio: 'inherit' });
} finally {
  fs.rmSync(tmp, { recursive: true, force: true });
}
