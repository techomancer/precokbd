#include <SPI.h>
#include <Wire.h>
#include "Adafruit_SSD1306.h"
#include "Keyboard.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library. 
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define ROWS 8
#define COLUMNS 8

unsigned char columnPins[COLUMNS] = { 10, 16, 14, 15, 18, 19, 20, 21 };
unsigned char rowPins[ROWS] = { 1, 0, 4, 5, 6, 7, 8, 9 };

#define KEY_FN 0x100

short kbdMatrix[8][8] = {
 { KEY_TAB, KEY_ESC, -1, -1, -1, -1, -1, -1 }, // 0
 { KEY_LEFT_CTRL, '1', KEY_INSERT, 'a', ' ', KEY_FN/*erase*/, 'z', 'q' }, // 1
 { '2', '3', 'w', 's', 'c', 'x', 'd', 'e' }, // 2
 { '4', '5', 'r', 'f', 'b', 'v', 'g', 't' }, // 3
 { '6', '7', 'y', 'h', 'm', 'n', 'j', 'u' }, // 4
 { '8', '9', 'i', 'k', '.', ',', 'l', 'o' }, // 5
 { '0', KEY_BACKSPACE /*repeat*/, 'p', ';', KEY_LEFT_SHIFT, '/', '\'', '=' }, // 6
 { -1, -1, -1, KEY_RIGHT_ARROW, KEY_LEFT_ALT /*ANSWER*/, KEY_DELETE, KEY_RETURN, KEY_LEFT_ARROW }  // 7
};

short kbdMatrixFn[8][8] = {
 { -1, '`', -1, -1, -1, -1, -1, -1 }, // 0
 { KEY_RIGHT_CTRL, KEY_F1, KEY_PAGE_UP, -1 /*a*/, ' ', KEY_FN/*erase*/, -1/*z*/, -1/*q*/ }, // 1
 { KEY_F2, KEY_F3, -1/*w*/, -1/*s*/, -1/*c*/, -1/*x*/, -1/*d*/, -1/*e*/ }, // 2
 { KEY_F4, KEY_F5, -1/*r*/, -1/*f*/, -1/*b*/, -1/*v*/, -1/*g*/, -1/*t*/ }, // 3
 { KEY_F6, KEY_F7, -1/*y*/, -1/*h*/, -1/*m*/, -1/*n*/, -1/*j*/, -1/*u*/ }, // 4
 { KEY_F8, KEY_F9, -1/*i*/, -1/*k*/, -1/*.*/, -1/*,*/, KEY_DOWN_ARROW, -1/*o*/ }, // 5
 { KEY_F10, '\\' /*repeat*/, KEY_UP_ARROW, '[', KEY_RIGHT_SHIFT, -1/*?*/, ']', '-' }, // 6
 { -1, -1, -1, KEY_END, KEY_RIGHT_ALT/*ANSWER*/, KEY_PAGE_DOWN, KEY_RETURN, KEY_HOME }  // 7
};

unsigned char state[ROWS];
#define HISTORY 5
unsigned char rolling_state[HISTORY][ROWS];
unsigned char curr_state = 0;
unsigned char fn_state[ROWS];

#define MOD_LEFT_SHIFT  0x01
#define MOD_RIGHT_SHIFT 0x02
#define MOD_SHIFT       0x03
#define MOD_LEFT_CTRL   0x04
#define MOD_RIGHT_CTRL  0x08
#define MOD_CTRL        0x0C
#define MOD_LEFT_ALT    0x10
#define MOD_RIGHT_ALT   0x20
#define MOD_ALT         0x30
#define MOD_FN          0x40

unsigned char modifier = 0;

void setup() {
  int i, j;

  for (i = 0; i < ROWS; i++) {
    state[i] = 0;
    for (j = 0; j < HISTORY; j++) {
      rolling_state[j][i] = 0;
    }
  }
  curr_state = 0;
  modifier = 0;

  // put your setup code here, to run once:
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  Keyboard.begin();
  // Clear the buffer
  display.clearDisplay();

  display.display();
}

uint8_t bmpFn[] = {
  0b01111111,0b11100000,
  0b10000000,0b00010000,
  0b10111000,0b00010000,
  0b10100000,0b00010000,
  0b10110011,0b10010000,
  0b10100010,0b01010000,
  0b10100010,0b01010000,
  0b10000000,0b00010000,
  0b01111111,0b11100000,
};

uint8_t bmpSh[] = {
  0b01111111,0b11110000,
  0b10000000,0b00001000,
  0b10011101,0b00001000,
  0b10100001,0b00001000,
  0b10011001,0b11001000,
  0b10000101,0b00101000,
  0b10111001,0b00101000,
  0b10000000,0b00001000,
  0b01111111,0b11110000,
};

void drawBMP(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t *b)
{
  uint8_t cx, cy, mask;
  for (cy = 0; cy < h; cy++) {
      mask = 0b10000000;
      for (cx = 0; cx < w; cx++) {
        if (*b & mask)
          display.drawPixel(x + cx, y + cy, SSD1306_WHITE);

        mask >>= 1;
        if (!mask) {
          mask = 0b10000000;
          b++;
        }
      }
      // last byte not full
      if (mask != 0b10000000)
        b++;
  }
}

void scanByRow(unsigned char *a) {
  int r, c;

  for (r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }
  
  for (c = 0; c < COLUMNS; c++) {
    pinMode(columnPins[c], INPUT_PULLUP);
  }

  for (r = 0; r < ROWS; r++) {
      unsigned char cur;
      unsigned char mask;
      digitalWrite(rowPins[r], LOW);

      for (c = 0, cur = 0, mask = 1; c < COLUMNS; c++) {
        if (digitalRead(columnPins[c]) == LOW)
            cur |= mask;
        mask <<= 1;
      }
      
      a[r] = cur;

      digitalWrite(rowPins[r], HIGH);
  }  
}

void scanByColumn(unsigned char *a) {
  int r, c;

  for (r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], INPUT_PULLUP);
    
  }
  
  for (c = 0; c < COLUMNS; c++) {
    pinMode(columnPins[c], OUTPUT);
    digitalWrite(columnPins[c], HIGH);
  }

  for (c = 0; c < COLUMNS; c++) {
      unsigned char cur;
      unsigned char mask = (1u << c);

      digitalWrite(columnPins[c], LOW);

      for (r = 0; r < ROWS; r++) {
        if (digitalRead(rowPins[r]) == LOW)
            a[r] |= mask;
      }

      digitalWrite(columnPins[c], HIGH);
  }  
}

void countBits(unsigned char *a) {
  int i, j, h;

  for (j = 0; j < ROWS; j++) {
    unsigned int r;
    unsigned char mask;

    for (i = 0, r = 0, mask = 1; i < COLUMNS; i++) {
      unsigned char c;
      for (h = 0, c = 0; h < HISTORY; h++) {
        if (rolling_state[h][j] & mask)
          c++;
      }

      if (c >= 3)
        r |= mask;

      mask <<= 1;
    }
    a[j] = r;
  }    
}

void pressKey(int w, int r) {
  int j;

  if (kbdMatrix[w][r] == KEY_FN) {
    modifier |= MOD_FN;
    // Initialize fn key state
    for (j = 0; j < ROWS; j++)
      fn_state[j] = 0;
    return;
  }

  if (modifier & MOD_FN) {
    switch (kbdMatrix[w][r]) {
      case -1:
        return;
      case KEY_RIGHT_SHIFT:
        modifier |= MOD_RIGHT_SHIFT;
        break;
      case KEY_RIGHT_CTRL:
        modifier |= MOD_RIGHT_CTRL;
        break;
      case KEY_RIGHT_ALT:
        modifier |= MOD_RIGHT_ALT;
        break;      
    }
    Keyboard.press(kbdMatrixFn[w][r]);
    fn_state[w] |= (1u << r);
  } else {  
    switch (kbdMatrix[w][r]) {
      case -1:
        return;
      case KEY_LEFT_SHIFT:
        modifier |= MOD_LEFT_SHIFT;
        break;
      case KEY_LEFT_CTRL:
        modifier |= MOD_LEFT_CTRL;
        break;
      case KEY_LEFT_ALT:
        modifier |= MOD_LEFT_ALT;
        break;      
    }
    Keyboard.press(kbdMatrix[w][r]);
  }
}

void repeatKey(int w, int r) {
  
}

void releaseKey(int w, int r) {
  int i, j;
  unsigned char mask;

  if (kbdMatrix[w][r] == KEY_FN) {
    // Release all keys pressed while FN was down.
    for (j = 0; j < ROWS; j++) {
      for (i = 0, mask = 1u; i < COLUMNS; i++, mask <<= 1) {
        if (fn_state[j] & mask) {
          releaseKey(j, i);
        }
      }
    }
    modifier &= ~MOD_FN;
    return;    
  }

  if (modifier & MOD_FN) {
    switch (kbdMatrixFn[w][r]) {
      case -1:
        return;
      case KEY_RIGHT_SHIFT:
        modifier &= ~MOD_RIGHT_SHIFT;
        break;
      case KEY_RIGHT_CTRL:
        modifier &= ~MOD_RIGHT_CTRL;
        break;
      case KEY_RIGHT_ALT:
        modifier &= ~MOD_RIGHT_ALT;
        break;
    }
    Keyboard.release(kbdMatrixFn[w][r]);
    fn_state[w] &= ~(1u << r);
  } else {  
    switch (kbdMatrix[w][r]) {
      case -1:
        return;
      case KEY_LEFT_SHIFT:
        modifier &= ~MOD_LEFT_SHIFT;
        break;
      case KEY_LEFT_CTRL:
        modifier &= ~MOD_LEFT_CTRL;
        break;
      case KEY_LEFT_ALT:
        modifier &= ~MOD_LEFT_ALT;
        break;
    }
    Keyboard.release(kbdMatrix[w][r]);
  }
}


void processKeys(unsigned char *os, unsigned char *ns) {
  int i, j;
  unsigned char mask;
  
  for (j = 0; j < ROWS; j++) {
    for (i = 0, mask = 1u; i < COLUMNS; i++, mask <<= 1) {
      if (os[j] & mask) {
        if (ns[j] & mask)
          repeatKey(j, i);
        else
          releaseKey(j, i);
      } else {
        if (ns[j] & mask)
          pressKey(j, i);
      }
    } // i
  } // j
}

void loop() {
  int i, j, x, y;
  unsigned char new_state[ROWS];

  curr_state++;
  if (curr_state >= HISTORY)
    curr_state = 0;

  scanByRow(rolling_state[curr_state]);
  scanByColumn(rolling_state[curr_state]);
  countBits(new_state);
  processKeys(state, new_state);
  
  display.clearDisplay();

  for (j = 0; j < ROWS; j++) {
      for (i = 0; i < COLUMNS; i++) {
         for (x = 0; x < 4; x++)
             for (y = 0; y < 4; y++) {
                if (x == 0 || y == 0)
                  display.drawPixel(i * 4 + x, j * 4 + y, SSD1306_WHITE);
                else if (new_state[j] & (1u << i))
                  display.drawPixel(i * 4 + x, j * 4 + y, SSD1306_WHITE);
             }
      }
  }
  if (modifier & MOD_SHIFT)
    drawBMP(128-12-13, 32-9, 13, 9, bmpSh);
  if (modifier & MOD_FN)
    drawBMP(128-12, 32-9, 12, 9, bmpFn);

  for (i = 0; i < ROWS; i++)
    state[i] = new_state[i];

  display.display();
  delayMicroseconds(100);
}

#if 0

pin 0: multiple choice, help
pin 1: 1, lock, q, a, z, ins, spc
pin 2: 3, 2, w, e, s, d, x, c
pin 3: 4, 5, r, t, f, g, v, b
pin 4: 6, 7, y, u, h, j, n, m
pin 5: 8, 9, i, o, k, l, "," , .
pin 6: 0, rep, ;, ', ?, rshift, lshift
pin 7: left, right, enter, del, ans 

receive

13: p, 
14: 9, y, r, w, ins
15: 8, 0, 6, 4, 2, help, lock


#endif
#if 0
short kbdMatrixUp[8][8] = {
 { KEY_TAB, KEY_ESC, -1, -1, -1, -1, -1, -1 }, // 0
 { KEY_LEFT_CTRL, '!', KEY_INSERT, 'A', ' ', KEY_LEFT_ALT  /*erase*/, 'Z', 'Q' }, // 1
 { '@', '#', 'W', 'S', 'C', 'X', 'D', 'E' }, // 2
 { '$', '%', 'R', 'F', 'B', 'V', 'G', 'T' }, // 3
 { '^', '&', 'Y', 'H', 'M', 'N', 'J', 'U' }, // 4
 { '*', '(', 'I', 'K', '>', '<', 'L', 'O' }, // 5
 { ')', KEY_BACKSPACE /*repeat*/, 'P', ':', KEY_LEFT_SHIFT, '?', '\"', '+' }, // 6
 { -1, -1, -1, KEY_RIGHT_ARROW, KEY_FN /* ANSWER/FN*/, KEY_DELETE, KEY_RETURN, KEY_LEFT_ARROW }  // 7
};
#endif
