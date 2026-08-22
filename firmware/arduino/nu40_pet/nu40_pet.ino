#include <bluefruit.h>

static const char SERVICE_UUID[] = "7d2a0001-7b7a-4f45-8d68-36f1a4d9c101";
static const char NOTIFY_UUID[]  = "7d2a0002-7b7a-4f45-8d68-36f1a4d9c101";
static const char WRITE_UUID[]   = "7d2a0003-7b7a-4f45-8d68-36f1a4d9c101";

BLEService petService(SERVICE_UUID);
BLECharacteristic boardToWeb(NOTIFY_UUID);
BLECharacteristic webToBoard(WRITE_UUID);
BLEDis deviceInfo;

uint32_t blinkMs = 760;
uint32_t lastBlink = 0;
uint32_t lastButton = 0;
bool ledsLit = false;
bool previousButton = HIGH;
uint8_t moodMask = 0b011; // okay: red + green

void setLeds(bool on) {
  digitalWrite(PIN_LED1, on && (moodMask & 0b001) ? HIGH : LOW);
  digitalWrite(PIN_LED2, on && (moodMask & 0b010) ? HIGH : LOW);
  digitalWrite(PIN_LED3, on && (moodMask & 0b100) ? HIGH : LOW);
}

void moodWritten(uint16_t connHandle, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  (void) connHandle;
  (void) chr;
  char command[97];
  uint16_t size = min((uint16_t)96, len);
  memcpy(command, data, size);
  command[size] = '\0';

  if (strstr(command, "happy")) {
    moodMask = 0b010;
    blinkMs = 1400;
  } else if (strstr(command, "hungry")) {
    moodMask = 0b001;
    blinkMs = 260;
  } else if (strstr(command, "okay")) {
    moodMask = 0b011;
    blinkMs = 760;
  }
}

void startAdvertising() {
  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(petService);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

void setup() {
  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_LED1, OUTPUT);
  pinMode(PIN_LED2, OUTPUT);
  pinMode(PIN_LED3, OUTPUT);
  setLeds(false);

  Serial.begin(115200);
  Bluefruit.begin(1, 0);
  Bluefruit.setName("NU-40 PET");
  Bluefruit.setTxPower(4);

  deviceInfo.setManufacturer("NUCODE");
  deviceInfo.setModel("NU-40 DK");
  deviceInfo.begin();

  petService.begin();
  boardToWeb.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  boardToWeb.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  boardToWeb.setMaxLen(32);
  boardToWeb.begin();

  webToBoard.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  webToBoard.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
  webToBoard.setMaxLen(96);
  webToBoard.setWriteCallback(moodWritten);
  webToBoard.begin();

  startAdvertising();
  Serial.println("NU-40 PET BLE advertising started");
}

void loop() {
  uint32_t now = millis();
  if (now - lastBlink >= blinkMs / 2) {
    lastBlink = now;
    ledsLit = !ledsLit;
    setLeds(ledsLit);
  }

  bool button = digitalRead(PIN_BUTTON1);
  if (previousButton == HIGH && button == LOW && now - lastButton > 180) {
    lastButton = now;
    const char event[] = "{\"type\":\"feed\"}";
    if (Bluefruit.connected()) boardToWeb.notify((uint8_t*) event, strlen(event));
  }
  previousButton = button;
  delay(5);
}
