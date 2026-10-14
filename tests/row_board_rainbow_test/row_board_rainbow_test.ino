// Row-board bring-up test — D1..D6 rainbow + per-LED identification
// Kosmo 16-channel sequencer, single row board ("fin") driven straight from a Teensy 4.1.
//
// Mapping taken verbatim from docs/16-channel-sequencer-hardware.md:
//   - LEDs are common ANODE: anode -> SEG line (pair select), 3 cathodes -> 3 DIG lines (colour)
//   - 8 SEG x 6 DIG = 48 dies; scan limit 0x05
//   - Pair->bit map is NOT the identity: PAIR_BIT = {6,5,4,3,2,1,0,7}
//   - Plane order within a half is B, G, R (DIG_0 is blue, not red)
//
// WIRING: see the notes in the chat / the doc. DIN=11, CLK=13, LOAD=10 — the same
// Teensy pins the main board uses, so this sketch stays valid once U33 is in the path.
//
// !! On the real board MAX_DIN/CLK/LOAD arrive at 5 V from U33 (74AHCT125). Driving the
// !! MAX7219 directly from 3.3 V bypasses that shifter and sits BELOW the datasheet's
// !! V_IH = 0.7 x V+ = 3.5 V. It often works; if it is dead or glitchy, suspect this
// !! before suspecting your soldering.

#include <SPI.h>

static const uint8_t PIN_DIN  = 11;  // MOSI
static const uint8_t PIN_CLK  = 13;  // SCK (also the on-board LED — it will flicker)
static const uint8_t PIN_LOAD = 10;  // CS / LOAD

// MAX7219 registers
static const uint8_t REG_NOOP      = 0x00;
static const uint8_t REG_DIGIT0    = 0x01;  // 0x01..0x06 = the six colour bit-planes
static const uint8_t REG_DECODE    = 0x09;
static const uint8_t REG_INTENSITY = 0x0A;
static const uint8_t REG_SCANLIMIT = 0x0B;
static const uint8_t REG_SHUTDOWN  = 0x0C;
static const uint8_t REG_DISPTEST  = 0x0F;

// 3-bit colour codes, 0bBGR
static const uint8_t C_OFF = 0, C_RED = 1, C_GRN = 2, C_YEL = 3,
                     C_BLU = 4, C_MAG = 5, C_CYA = 6, C_WHT = 7;

// Hue wheel — six steps, which is exactly the six LEDs that are fitted.
static const uint8_t WHEEL[6] = { C_RED, C_YEL, C_GRN, C_CYA, C_BLU, C_MAG };

// As drawn: pair 0 (cols 1-2) is on SEG A = bit 6, pair 7 (cols 15-16) on SEG DP = bit 7.
static const uint8_t PAIR_BIT[8] = { 6, 5, 4, 3, 2, 1, 0, 7 };

static uint8_t colour[16];        // one 3-bit code per column, index = column - 1
static uint8_t intensity = 0x08;  // mid — 0x02 is dim enough to read as "not working"
static char    mode      = 'r';
static uint8_t only_led  = 0;     // 1..6 in 'o' mode

static void maxWrite(uint8_t reg, uint8_t val) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_LOAD, LOW);
  SPI.transfer(reg);
  SPI.transfer(val);
  digitalWrite(PIN_LOAD, HIGH);
  SPI.endTransaction();
}

// Transpose 16 colour codes into the 6 digit registers. Straight from the doc.
static void pushFrame() {
  uint8_t plane[6] = { 0, 0, 0, 0, 0, 0 };
  for (uint8_t i = 0; i < 16; i++) {
    uint8_t bit  = PAIR_BIT[i >> 1];
    uint8_t base = (i & 1) ? 3 : 0;   // even (upper) column's planes start at DIG_3
    uint8_t c    = colour[i];
    if (c & 4) plane[base + 0] |= (1 << bit);   // blue  -> DIG_0 / DIG_3
    if (c & 2) plane[base + 1] |= (1 << bit);   // green -> DIG_1 / DIG_4
    if (c & 1) plane[base + 2] |= (1 << bit);   // red   -> DIG_2 / DIG_5
  }
  for (uint8_t d = 0; d < 6; d++) maxWrite(REG_DIGIT0 + d, plane[d]);
}

static void initChip() {
  maxWrite(REG_SHUTDOWN,  0x00);   // shut down while configuring
  maxWrite(REG_DISPTEST,  0x00);
  maxWrite(REG_DECODE,    0x00);   // no decode — raw segment control
  maxWrite(REG_SCANLIMIT, 0x05);   // DIG0..DIG5 -> 1/6 duty
  maxWrite(REG_INTENSITY, intensity);
  for (uint8_t d = 0; d < 6; d++) maxWrite(REG_DIGIT0 + d, 0x00);
  maxWrite(REG_SHUTDOWN,  0x01);   // normal operation
}

static void help() {
  Serial.println();
  Serial.println(F("Row-board test — commands:"));
  Serial.println(F("  r  rainbow chase across D1..D6   (default)"));
  Serial.println(F("  s  static: D1=red D2=grn D3=blu D4=yel D5=cya D6=mag"));
  Serial.println(F("  w  all six white"));
  Serial.println(F("  t  MAX7219 display test (all dies on, bypasses my mapping)"));
  Serial.println(F("  p  plane walk: 0xFF into one digit register at a time"));
  Serial.println(F("  1..6  light only that LED, white  — confirms which LED is which column"));
  Serial.println(F("  0  all off"));
  Serial.println(F("  +/-  intensity up/down"));
  Serial.println(F("  ?  this help"));
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LOAD, OUTPUT);
  digitalWrite(PIN_LOAD, HIGH);
  SPI.begin();

  delay(200);
  initChip();

  // Display test first: it lights every die without going through my transpose, so it
  // separates "wiring/chip is dead" from "my mapping is wrong".
  maxWrite(REG_DISPTEST, 0x01);
  delay(1500);
  maxWrite(REG_DISPTEST, 0x00);

  help();
  Serial.println(F("mode=rainbow"));
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 's' || c == 'w' || c == 't' || c == 'p') {
      mode = c;
      if (c == 't') { maxWrite(REG_DISPTEST, 0x01); }
      else          { maxWrite(REG_DISPTEST, 0x00); }
      Serial.print(F("mode=")); Serial.println(c);
    } else if (c >= '1' && c <= '6') {
      mode = 'o'; only_led = c - '0';
      maxWrite(REG_DISPTEST, 0x00);
      Serial.print(F("only D")); Serial.println(only_led);
    } else if (c == '0') {
      mode = 'x';
      maxWrite(REG_DISPTEST, 0x00);
      Serial.println(F("all off"));
    } else if (c == '+' || c == '-') {
      if (c == '+' && intensity < 0x0F) intensity++;
      if (c == '-' && intensity > 0x00) intensity--;
      maxWrite(REG_INTENSITY, intensity);
      Serial.print(F("intensity=0x")); Serial.println(intensity, HEX);
    } else if (c == '?') {
      help();
    }
  }

  static uint32_t last = 0;
  static uint8_t  phase = 0;
  if (millis() - last < 120) return;
  last = millis();

  for (uint8_t i = 0; i < 16; i++) colour[i] = C_OFF;

  switch (mode) {
    case 'r':
      for (uint8_t i = 0; i < 6; i++) colour[i] = WHEEL[(i + phase) % 6];
      phase = (phase + 1) % 6;
      break;
    case 's': {
      static const uint8_t fixed[6] = { C_RED, C_GRN, C_BLU, C_YEL, C_CYA, C_MAG };
      for (uint8_t i = 0; i < 6; i++) colour[i] = fixed[i];
      break;
    }
    case 'w':
      for (uint8_t i = 0; i < 6; i++) colour[i] = C_WHT;
      break;
    case 'o':
      if (only_led >= 1 && only_led <= 6) colour[only_led - 1] = C_WHT;
      break;
    case 't':
      return;  // chip is driving itself

    case 'p': {
      // Decisive diagnostic: 0xFF into one digit register lights every SEG line during
      // that DIG's slot, with no transpose involved. If this walks correctly, the chip and
      // the matrix are good and only the colour[] -> plane[] mapping can be at fault.
      static uint8_t pl = 0, tick = 0;
      if (tick == 0) {
        for (uint8_t d = 0; d < 6; d++) maxWrite(REG_DIGIT0 + d, (d == pl) ? 0xFF : 0x00);
        Serial.print(F("plane ")); Serial.print(pl);
        Serial.print(F(" = digit reg 0x0")); Serial.print(pl + 1);
        Serial.print(F(" -> expect "));
        if (pl < 3) Serial.print(F("D1 D3 D5 (odd cols), "));
        else        Serial.print(F("D2 D4 D6 (even cols), "));
        uint8_t k = pl % 3;
        if      (k == 0) Serial.println(F("BLUE"));
        else if (k == 1) Serial.println(F("GREEN"));
        else             Serial.println(F("RED"));
        pl = (pl + 1) % 6;
      }
      tick = (tick + 1) % 8;   // ~1 s per plane
      return;
    }
    default:
      break;   // 'x' = all off
  }
  pushFrame();
}
