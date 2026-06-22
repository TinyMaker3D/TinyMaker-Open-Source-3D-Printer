extern File myfile;
extern PNG png;
extern Arduino_GFX *gfx1;

// 9.6KB 1-Bit RAM Buffer (320x240 pixels)
// This completely separates the SD card SPI traffic from the LCD SPI traffic.
uint8_t maskBuffer[9600];

void * myOpen(const char *filename, int32_t *size) {
  myfile = SD.open(filename);
  if (myfile) {
    *size = myfile.size();
    return &myfile;
  }
  return NULL; 
}

void myClose(void *handle) {
  if (myfile) myfile.close();
}

int32_t myRead(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  if (!myfile) return 0;
  return myfile.read(buffer, length);
}

int32_t mySeek(PNGFILE *handle, int32_t position) {
  if (!myfile) return 0;
  return myfile.seek(position);
}

int PNGDraw(PNGDRAW *pDraw) {
  uint16_t usPixels[320];
  png.getLineAsRGB565(pDraw, usPixels, PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);

  int drawWidth = pDraw->iWidth;
  if (drawWidth > 320) drawWidth = 320;
  int y = pDraw->y;

  for (int x = 0; x < drawWidth; x++) {
      int byteIdx = (y * 320 + x) / 8;
      int bitIdx = 7 - (x % 8);
      
      if (usPixels[x] >= 0x8000) {
          // Light pixel (Model) -> UV should pass. Set bit to 1 (White).
          maskBuffer[byteIdx] |= (1 << bitIdx);
      } else {
          // Dark pixel (Background) -> UV blocked. Set bit to 0 (Black).
          maskBuffer[byteIdx] &= ~(1 << bitIdx);
      }
  }
  return 1; 
}

void print_next_png() {
  // 1. Wipe the RAM buffer to 0 (Black/Blocked)
  memset(maskBuffer, 0, sizeof(maskBuffer));
  
  FileName = "/";
  FileName += foldersel_long;
  FileName += "/";
  
  int i = current_layer;

  if (Layer_Height > 0.06)
    i = current_layer * 2 - 1; 
    
  FileName += i;
  FileName += ".png";
  
  char NameChar[110];
  FileName.toCharArray(NameChar, 110);

  Serial.print("[PNG] Opening: ");
  Serial.println(NameChar);
  
  // 2. Read the entire PNG from SD into the RAM buffer.
  //    SD card is on SPI — keep it completely closed before touching the LCD.
  int rc = png.open((const char *)NameChar, myOpen, myClose, myRead, mySeek, PNGDraw);
  if (rc == PNG_SUCCESS) {
    rc = png.decode(NULL, 0);
    if (rc != PNG_SUCCESS) {
      Serial.print("[WARN] PNG decode failed, rc="); Serial.println(rc);
    }
    png.close();
  } else {
    Serial.print("[WARN] PNG open failed, rc="); Serial.println(rc);
    // Buffer is all-zeros (black) — that is the safe fallback, don't push white.
    return;
  }

  // 3. SD card is now fully closed.
  //    Re-initialise gfx1 before drawing to recover from any SPI bus
  //    state left behind by gfx2 or the SD card transaction.
  gfx1->begin();
  delay(5);

  // 4. Push the 1-bit buffer to the UV LCD.
  //    1 = White (0xFFFF, UV passes), 0 = Black (0x0000, UV blocked)
  gfx1->drawBitmap(0, 0, maskBuffer, 320, 240, 0xFFFF, 0x0000);

  Serial.println("[PNG] Frame pushed to UV screen.");
  delay(50);  
}
