/*
 * This is a full-fledged test routine used to validate Phoenard operation.
 * All connections to on-board components are tested where possible.
 * For sensors, an additional damage check is performed.
 * This means sample data is measured and the range/deviation is checked.
 * Heavily fluctuating or extreme outputs indicate hardware failure.
 *
 * For testing the connector, the Phoenard must be tested in our test station.
 * This test station also performs the flashing of the bootloader/uploading.
 * On startup a check is performed whether the test station is active.
 *
 * All tests are located in the tests.h file.
 */
#include "Phoenard.h"
#include "TestResult.h"

int testCnt = 0;
boolean isStationConnected = false;
uint32_t testroutine_crc;
const uint32_t testroutine_length = 20000;

// Create a test result buffer of sufficient size
TestResult test_results[15];

TestResult doTest(const char* what, TestResult(*testFunc)(void)) {
  // Check if pressed - if pressed for longer than 1 second, show message
  long sel_start = millis();
  while(isSelectPressed() && (millis() - sel_start) < 1000);

  // Wait until the SELECT key is no longer pressed
  if (isSelectPressed()) {
    showMessage("Please release the SELECT button\n"
                "Indicates hardware problem if not pressed");

    while (isSelectPressed());
    showMessage("");
  }
  
  Serial.print(what);
  Serial.print(" Testing...");
  Serial.flush();
  
  // Show testing state
  showMessage("");
  showStatus(testCnt, YELLOW, what, "Testing...");

  // Do the test
  TestResult result = testFunc();
  strcpy(result.device, what);
  test_results[testCnt] = result;

  // Show result
  showStatus(testCnt, result.success ? GREEN : RED, what, result.status);
  Serial.print(result.success ? "  SUCCESS" : "  FAILURE");
  Serial.print(" - ");
  Serial.println(result.status);
  Serial.flush();

  // Next test
  testCnt++;
  
  return result;
}

void showStatus(int index, color_t color, const char* what, const char* text) {
  const int w = 310;
  const int h = 13;
  int x = 5;
  int y = 5 + index * (h + 3);
  display.fillRect(x, y, w, h, color);
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(x + 3, y + 3);
  display.print(what);
  display.print(": ");
  display.print(text);
}

void printHex(uint8_t val) {
  Serial.print("0x");
  if (val < 16) Serial.print('0');
  Serial.print(val, HEX);
  Serial.print(", ");
  Serial.flush();
}

/* Calculates the 32-bit CRC for a region of flash memory by reading word-by-word */
uint32_t calcCRCFlash(uint32_t start_addr, uint32_t end_addr, uint32_t start_crc) {
  uint32_t crc = ~start_crc;
  uint32_t address = start_addr;
  uint32_t address_ctr = (end_addr - start_addr) / 2;
  do {
    runCRCWord(pgm_read_word_far(address), &crc);
    address += 2;
  } while (--address_ctr);
  crc = ~crc;
  return crc;
}

/* Calculates the 32-bit CRC for a region of flash memory by reading rapidly in bursts of 16 bytes 
 * The result of this reading is that flash is more heavily loaded, increasing the likelyhood of errors */
uint32_t calcCRCFlashBurst(uint32_t start_addr, uint32_t end_addr, uint32_t start_crc) {
  uint32_t crc = ~start_crc;
  uint32_t address = start_addr;
  uint32_t address_ctr = (end_addr - start_addr) / 2;
  uint16_t buff[8];
  const uint16_t buff_len = sizeof(buff)/sizeof(buff[0]);
  uint16_t *p = buff+buff_len;
  do {
    if (p == (buff+buff_len)) {
      for (uint8_t k = 0; k < buff_len; k++) {
        buff[k] = pgm_read_word_far(address);
        address += 2;
      }
      p = buff;
    }
    runCRCWord(*(p++), &crc);
  } while (--address_ctr);
  crc = ~crc;
  return crc;
}

void runCRCWord(uint16_t wordValue, uint32_t* crc) {
  for (unsigned char k = 16; k; k--) {
    if (!(k & 0x7)) {
      *crc ^= (wordValue & 0xFF);
      wordValue >>= 8;
    }
    unsigned char m = (*crc & 0x1);
    *crc >>= 1;
    if (m) *crc ^= 0xEDB88320;
  }
}

void setup() {
  Serial.begin(57600);

  delay(20);

  // Send out a token indicating we request a test station
  Serial.println(F("Ready"));

  // Wait for a very short time for a response back to initiate test station mode
  isStationConnected = readToken(Serial, "Station OK", 200);

  // Reset EEPROM settings upon next reset
  // This resets screen calibration and tells it to load the sketch list
  if (isStationConnected) {
    PHN_Settings settings;
    PHN_Settings_Load(settings);
    settings.flags &= ~(SETTINGS_TOUCH_HOR_INV | SETTINGS_TOUCH_VER_INV);
    settings.flags |= SETTINGS_DEFAULT_FLAGS;
    settings.touch_hor_a = SETTINGS_DEFAULT_TOUCH_HOR_A;
    settings.touch_hor_b = SETTINGS_DEFAULT_TOUCH_HOR_B;
    settings.touch_ver_a = SETTINGS_DEFAULT_TOUCH_VER_A;
    settings.touch_ver_b = SETTINGS_DEFAULT_TOUCH_VER_B;
    char defaultSketch[] = SETTINGS_DEFAULT_SKETCH;
    memcpy(settings.sketch_toload, defaultSketch, 8);
    PHN_Settings_Save(settings);
  }

  // Turn on LED Pin 13
  pinMode(13, OUTPUT);
  digitalWrite(13, LOW);

  // LCD Test
  startLCDTest();

  // Indicate testing has started
  Serial.println(F("Testing started."));

  // Turn on SIM module as needed
  // Do this in two steps to save time
  sim.powerOnStart();

  doTest("LCD Screen", testScreen);
  doTest("Connector", testConnector);
  doTest("Flash", testFlash);
  doTest("BMP180", testBMP180);
  doTest("MPU6050", testMPU6050);
  doTest("HMC5883L", testHMC5883L);
  doTest("SRAM", testRAM);
  doTest("Micro-SD", testSD);
  doTest("MP3", testMP3);
  doTest("MIDI", testMIDI);
  doTest("WiFi", testWiFi);
  doTest("Bluetooth", testBluetooth);

  // Wait until sim is powered on fully
  sim.powerOnEnd();

  // Perform testing of SIM908
  doTest("SIM908", testSIM);

  // Newline for spacing
  Serial.println();

  // All done! Check for errors and the like
  boolean test_success = true;
  for (int i = 0; i < testCnt; i++) {
    if (!test_results[i].success) {
      // Indicate the test was not successful
      if (test_success) {
        test_success = false;
        Serial.println(F("Testing completed with errors."));
        Serial.println(F("Please check the following components:"));
      }
      // Proceed to print all components that failed the test
      Serial.print("- ");
      Serial.print(test_results[i].device);
      Serial.print(" (");
      Serial.print(test_results[i].status);
      Serial.println(")");
    }
  }
  if (test_success) {
    Serial.println(F("Testing completed: no problems found."));
  }
}

void loop() {
}

void startLCDTest() {
  uint32_t crc, service_crc;
  uint32_t end_address = 0x40000;
  do {
    if (pgm_read_word_far(end_address-2) != 0xFFFF) {
      break;
    }
    end_address -= 2;
  } while (end_address != 0x3E000);

  // Read the firmware and flash information
  service_crc = calcCRCFlash(0x3E000, 0x3E100, 0);
  crc = calcCRCFlash(0x3E100, end_address, service_crc);
  testroutine_crc = calcCRCFlashBurst(0, testroutine_length, 0);

  // As a first test, show RGB colors on the screen to verify readout works as expected
  display.fillRect(0, 0, 107, 120, RED);
  display.fillRect(107, 0, 106, 120, GREEN);
  display.fillRect(213, 0, 107, 120, BLUE);
  display.setCursor(30, 30);
  display.setTextColor(WHITE);
  display.setTextSize(3);
  display.print(F("LCD Test Screen"));
  display.setTextSize(2);
  display.setCursor(70, 80);
  display.print(F("Press SELECT to"));
  display.setCursor(70, 100);
  display.print(F("start the test"));

  // Show Firmware information
  const char *service_token = (service_crc == 0xBBC8FBD5) ? "-Y" : "-S";
  Serial.print(F("Firmware crc: "));
  Serial.print(crc, HEX);
  Serial.println(service_token);
  display.setCursor(5, 5);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.print(crc, HEX);
  display.print(service_token);

  // Show testroutine flash CRC information for integrity check
  Serial.print(F("Test Routine crc: "));
  Serial.println(testroutine_crc, HEX);
  display.setCursor(264, 5);
  display.print(testroutine_crc, HEX);

  // Show message to serial to indicate testing can be started
  Serial.println(F("Press SELECT to continue..."));

  // Black-White color gradient
  color_t bw_gradient[320];
  for (int i = 0; i <= 319; i++) {
    uint8_t component = (uint8_t) ((float) i / 319.0 * 255.0);
    bw_gradient[i] = PHNDisplayHW::color565(component, component, component);
  }

  // Wait for SELECT pressed
  PHNDisplayHW::setViewport(0, 120, PHNDisplayHW::WIDTH-1, PHNDisplayHW::HEIGHT-1);
  PHNDisplayHW::setCursor(0, 120, DIR_RIGHT);
  while (!isSelectPressed()) {
    // R/G/B pixels continuously drawn
    for (int y = 0; y < 80; y++) {
      PHNDisplay16Bit::writePixels(RED, 107);
      PHNDisplay16Bit::writePixels(GREEN, 106);
      PHNDisplay16Bit::writePixels(BLUE, 107);
    }

    // 8-bit alternating b/w colors drawn
    uint8_t color_8 = 0x00;
    for (uint16_t p = 0; p < (20*PHNDisplayHW::WIDTH); p++) {
      PHNDisplay8Bit::writePixel(color_8);
      color_8 ^= 0xFF;
    }
    
    // 16-bit b-w gradient drawn
    for (int y = 0; y < 20; y++) {
      for (int x = 0; x < 320; x++) {
        PHNDisplay16Bit::writePixel(bw_gradient[x]);
      }
    }
  }
  PHNDisplayHW::setViewport(0, 0, PHNDisplayHW::WIDTH-1, PHNDisplayHW::HEIGHT-1);

  // Wipe screen
  display.fill(BLACK);
}
