#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <RTClib.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "time.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// OLED Display Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// RTC Configuration
RTC_DS3231 rtc;



// Button Configuration
#define BTN_UP 6
#define BTN_DOWN 9
#define BTN_LEFT 8
#define BTN_RIGHT 7
#define BTN_CENTER 10

// Button state storage
volatile bool buttonStates[5] = {false, false, false, false, false};
const uint8_t buttonPins[5] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_CENTER};
uint8_t statusIndex = 0;
bool isStatusActive = false; // Set to true when loading or active status needs displaying

// State Machine
enum ScreenState {
  SCREEN_WATCHFACE, SCREEN_MENU, SCREEN_STOPWATCH, SCREEN_TIMER, SCREEN_TIMER_ALERT,
  // Radio App
  SCREEN_RADIO,
  SCREEN_RADIO_WIFI, SCREEN_WIFI_CONFIRM, SCREEN_WIFI_SCANNING,
  SCREEN_WIFI_RESULTS, SCREEN_WIFI_PASSWORD, SCREEN_WIFI_KEYBOARD,
  SCREEN_WIFI_HOME, SCREEN_WIFI_TOGGLE_CONFIRM,
  SCREEN_RADIO_BLE, SCREEN_RADIO_ESPNOW, SCREEN_RADIO_POWER,
  SCREEN_WIFI_NTP
};
ScreenState currentScreen = SCREEN_WATCHFACE;

// Input Edge Detection
bool lastButtonStates[5] = {false, false, false, false, false};
bool buttonJustPressed[5] = {false, false, false, false, false};

// Menu State
const int NUM_MENU_ITEMS = 4;
const char* menuItems[NUM_MENU_ITEMS] = {"Stopwatch", "Timer", "Radio", "Lock"};
int menuIndex = 0;

// Stopwatch State
unsigned long swStartTime = 0;
unsigned long swElapsedTime = 0;
bool swRunning = false;
int swFocus = 0; // 0 = Back, 1 = Start/Pause, 2 = Stop/Reset

// Timer State
enum TimerMode { TM_SETTING, TM_READY, TM_RUNNING, TM_RINGING };
TimerMode tmMode = TM_SETTING;
int tmHours = 0, tmMinutes = 0, tmSeconds = 0;
int tmActiveUnit = 2; // Default to seconds
int tmFocus = 1;      // Default to [+]
unsigned long tmRemainingMillis = 0;
unsigned long tmLastTick = 0;
unsigned long tmSetPressStart = 0;
bool tmLongPressTriggered = false;

// Transition Animation
bool isAnimating = false;
int animOffsetY = 0;
int animTargetY = 0;
ScreenState animNextScreen = SCREEN_WATCHFACE;

// Power Management
unsigned long lastActivityTime = 0;
const unsigned long DISPLAY_TIMEOUT_MS = 5000;
bool displayOn = true;

// ============================================================
// RADIO APP STATE
// ============================================================

// --- Keyboard ---
enum KBMode { KB_LOWER, KB_UPPER, KB_SYMBOL };
KBMode currentKBMode = KB_LOWER;
int kbCursorRow = 0, kbCursorCol = 0;
// 3 rows × 10 cols (number row removed as requested)
// Row 0: QWERTYUIOP / !@#$%^&*()
// Row 1: ASDFGHJKL / 1234567890
// Row 2: ZXCVBNM / symbols
// Row 3: special keys
const char* kbLower[4][11] = {
  {"q","w","e","r","t","y","u","i","o","p","."},
  {"a","s","d","f","g","h","j","k","l",",","?"},
  {"z","x","c","v","b","n","m","!","^","^","<"},
  {"1","2","3","4","5","6","7","8","9"," ","EN"}
};
const char* kbUpper[4][11] = {
  {"Q","W","E","R","T","Y","U","I","O","P","."},
  {"A","S","D","F","G","H","J","K","L",",","?"},
  {"Z","X","C","V","B","N","M","!","^","^","<"},
  {"!","@","#","$","%","^","&","*","("," ","EN"}
};
const char* kbSymbol[4][11] = {
  {"1","2","3","4","5","6","7","8","9","0","~"},
  {"@","$","%","&","*","-","+","=","!","|","\\"},
  {"/","?",";",":","'","(",")","#","^","^","<"},
  {"[","]","{","}","<",">","^","_","."," ","EN"}
};

// --- WiFi ---
bool wifiEnabled = false;
int wifiScanResults = 0;
int wifiListIndex = -1; // -1 = back selected
int wifiListScroll = 0;
String wifiTargetSSID = "";
String wifiPassword = "";
String wifiStatusMsg = "";
bool wifiConnecting = false;
unsigned long wifiScanStart = 0;
unsigned long wifiConnectStartTime = 0;
int wifiHomeCursor = -1; // -1 = back, 0..N = items
int wifiConfirmCursor = 0; // 0=YES 1=NO
int wifiToggleCursor = 0;
int wifiPasswordCursor = 1; // 0=KEYBOARD, 1=CONNECT
unsigned long ntpSyncStart = 0;
unsigned long ntpSyncEnd = 0;
int ntpSyncState = 0; // 0=requesting, 1=success, 2=failed
int wifiRetryCount = 0;

// Input buffer for keyboard
String kbInputBuffer = "";
ScreenState kbReturnScreen = SCREEN_WIFI_PASSWORD;

// --- Saved credentials (up to 5 networks) ---
Preferences prefs;
const int MAX_SAVED_NETS = 5;
String savedSSIDs[MAX_SAVED_NETS];
String savedPWDs[MAX_SAVED_NETS];
int savedNetCount = 0;

// --- Internet pingers ---
bool pingerStatus[5] = {false, false, false, false, false};
bool internetOK = false;
volatile bool statusChanged = false;

// NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800; // IST = UTC+5:30
const int daylightOffset_sec = 0;

// Radio home cursor
int radioCursor = 0; // 0=WiFi,1=BLE,2=ESP-NOW,3=Power

// Prototypes
void updateDisplay();
void drawScreen(ScreenState screen, int yOffset);
void drawHeader(int yOffset, const char* appName = nullptr, bool backFocused = false);
void drawWatchFace(int yOffset);
void drawMenu(int yOffset);
void drawStopwatch(int yOffset);
void drawTimer(int yOffset);
void drawTimerAlert(int yOffset);
void drawCenteredText(Adafruit_SSD1306 &d, const String &text, int16_t y, uint8_t size = 1);
void drawBoxedCenteredText(Adafruit_SSD1306 &d, const char* text, int x, int y, int w, int h, bool inverted);
void startAnimation(ScreenState next, int targetOffset);
void buttonReadTask(void *pvParameters);
void printButtonStates();
// Radio prototypes
void drawRadioHome(int yOffset);
void drawRadioWifi(int yOffset);
void drawWifiConfirm(int yOffset);
void drawWifiScanning(int yOffset);
void drawWifiResults(int yOffset);
void drawWifiPassword(int yOffset);
void drawWifiKeyboard();
void drawWifiHome(int yOffset);
void drawWifiToggleConfirm(int yOffset);
void drawRadioStub(int yOffset, const char* name);
void loadCredentials();
void saveCredential(String ssid, String pwd);
void forgetCredential(String ssid);
String findSavedPwd(String ssid);
void syncTimeWithNTP();
void startWifiPingers();
void internetPinger1(void *p);
void internetPinger2(void *p);
void internetPinger3(void *p);
void internetPinger4(void *p);
void internetPinger5(void *p);
void connectivityAggregator(void *p);

void setup() {
  Serial.begin(115200);
  delay(100);
  
  // Initialize I2C on pins 4 (SDA) and 5 (SCL)
  Wire.begin(4, 5);
  delay(100);
  
  // Initialize OLED Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.println("Initializing...");
  display.display();
  delay(500);
  
  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
    display.clearDisplay();
    display.setTextSize(1);
    display.println("RTC Error!");
    display.display();
    while (1);
  }
  
  // Uncomment to set RTC time (set once, then comment out)
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  
  // Initialize Button Pins
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);
  
  // Create FreeRTOS task for reading buttons
  xTaskCreate(
    buttonReadTask,      // Task function
    "ButtonReader",      // Task name
    2048,                // Stack size
    NULL,                // Parameters
    1,                   // Priority
    NULL                 // Task handle
  );
  
  Serial.println("Display and RTC initialized!");
  
  // Load saved WiFi credentials from NVS
  loadCredentials();
  
  // Start internet pinger tasks
  startWifiPingers();
  
  // Enable Modem Sleep for power saving
  WiFi.setSleep(true);
  
  lastActivityTime = millis();
}

void loop() {
  bool activityDetected = false;

  // Edge detection logic
  for (int i = 0; i < 5; i++) {
    buttonJustPressed[i] = buttonStates[i] && !lastButtonStates[i];
    lastButtonStates[i] = buttonStates[i];
    if (buttonStates[i]) activityDetected = true;
  }

  // Handle Display Wake/Sleep
  if (activityDetected) {
    lastActivityTime = millis();
    if (!displayOn) {
      display.ssd1306_command(SSD1306_DISPLAYON);
      displayOn = true;
      currentScreen = SCREEN_WATCHFACE; // Always return to watchface on wake
      animOffsetY = 0;
      isAnimating = false;
      // We don't process the initial wake-up click as an action
      // to prevent accidental clicks when waking the watch
      for (int i = 0; i < 5; i++) buttonJustPressed[i] = false;
    }
  }

  // Check timeout (only inhibit sleep if stopwatch running, timer ringing, or WiFi scanning/connecting)
  bool inhibitSleep = (currentScreen == SCREEN_STOPWATCH && swRunning) || 
                      (currentScreen == SCREEN_TIMER && tmMode == TM_RINGING) ||
                      (currentScreen == SCREEN_TIMER_ALERT) ||
                      (currentScreen == SCREEN_WIFI_SCANNING) ||
                      (currentScreen == SCREEN_WIFI_PASSWORD && wifiConnecting) ||
                      (currentScreen == SCREEN_WIFI_NTP);

  if (displayOn && !inhibitSleep) {
    if (millis() - lastActivityTime > DISPLAY_TIMEOUT_MS) {
      display.ssd1306_command(SSD1306_DISPLAYOFF);
      displayOn = false;
    }
  }

  // Turn screen ON automatically if stopwatch is running and we are on the stopwatch screen
  if (!displayOn && currentScreen == SCREEN_STOPWATCH && swRunning) {
      display.ssd1306_command(SSD1306_DISPLAYON);
      displayOn = true;
      lastActivityTime = millis();
  }

  // Poll WiFi scan results OUTSIDE the displayOn gate so results are never missed
  if (currentScreen == SCREEN_WIFI_SCANNING && !isAnimating) {
    lastActivityTime = millis(); // keep display alive
    int result = WiFi.scanComplete();
    static bool scanRetried = false;
    
    if (result >= 0) {
      wifiScanResults = result;
      wifiListIndex = 0; wifiListScroll = 0;
      startAnimation(SCREEN_WIFI_RESULTS, -64);
      scanRetried = false; // Reset for next time
    } else if (result == WIFI_SCAN_FAILED) {
      if (!scanRetried) {
        scanRetried = true;
        WiFi.scanNetworks(true, false, false, 80);
      } else {
        wifiScanResults = 0;
        startAnimation(SCREEN_WIFI_RESULTS, -64);
        scanRetried = false;
      }
    } else if (millis() - wifiScanStart > 8000) {
      WiFi.scanDelete(); // Cleanup on timeout
      wifiScanResults = 0;
      startAnimation(SCREEN_WIFI_RESULTS, -64);
      scanRetried = false;
    }
  }

  // Poll NTP async
  if (currentScreen == SCREEN_WIFI_NTP && !isAnimating) {
    lastActivityTime = millis();
    if (ntpSyncState == 0) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo, 0)) {
        rtc.adjust(DateTime(
          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
        ntpSyncState = 1;
        ntpSyncEnd = millis();
      } else if (millis() - ntpSyncStart > 5000) {
        ntpSyncState = 2; // failed timeout
        ntpSyncEnd = millis();
      }
    } else {
      if (millis() - ntpSyncEnd > 2000) { // wait 2 sec on error or success
        startAnimation(SCREEN_WIFI_HOME, -64);
        ntpSyncState = 0;
      }
    }
  }

  // Only process UI if display is on
  if (displayOn) {
    // Handle Animations
    if (isAnimating) {
      if (animOffsetY < animTargetY) animOffsetY += 8;
      else if (animOffsetY > animTargetY) animOffsetY -= 8;
      
      if (abs(animOffsetY - animTargetY) < 8) {
        animOffsetY = 0;
        currentScreen = animNextScreen;
        isAnimating = false;
      }
    } else {
      // Handle Input based on current screen
      if (currentScreen == SCREEN_WATCHFACE) {
        if (buttonJustPressed[0]) { // UP button -> Menu
          startAnimation(SCREEN_MENU, -64); // Slide up
        }
      } 
      else if (currentScreen == SCREEN_MENU) {
        if (buttonJustPressed[0]) { // UP
          menuIndex = (menuIndex - 1 + NUM_MENU_ITEMS) % NUM_MENU_ITEMS;
        }
        if (buttonJustPressed[1]) { // DOWN
          menuIndex = (menuIndex + 1) % NUM_MENU_ITEMS;
        }
        if (buttonJustPressed[4]) { // CENTER (Click)
          if (menuIndex == 0) {
          swFocus = 1;
          startAnimation(SCREEN_STOPWATCH, -64);
        } else if (menuIndex == 1) { // Timer
          tmMode = TM_SETTING;
          tmFocus = 1;
          startAnimation(SCREEN_TIMER, -64);
        } else if (menuIndex == 2) { // Radio
          radioCursor = 0;
          startAnimation(SCREEN_RADIO, -64);
        } else if (menuIndex == 3) { // Lock
          startAnimation(SCREEN_WATCHFACE, 64);
        }
        }
      }
      else if (currentScreen == SCREEN_STOPWATCH) {
        // Stopwatch Navigation
        if (buttonJustPressed[0]) { // UP
          if (swFocus == 1 || swFocus == 2) swFocus = 0; // Go up to back button
        }
        if (buttonJustPressed[1]) { // DOWN
          if (swFocus == 0) swFocus = 1; // Go down to start button
        }
        if (buttonJustPressed[2]) { // LEFT
          if (swFocus == 2) swFocus = 1; // From reset to start
        }
        if (buttonJustPressed[3]) { // RIGHT
          if (swFocus == 1) swFocus = 2; // From start to reset
        }
        
        if (buttonJustPressed[4]) { // CENTER (Click)
          if (swFocus == 0) { // Back
            startAnimation(SCREEN_MENU, 64);
          } else if (swFocus == 1) { // START/PAUSE
            if (swRunning) {
              swElapsedTime += millis() - swStartTime;
              swRunning = false;
            } else {
              swStartTime = millis();
              swRunning = true;
            }
          } else if (swFocus == 2) { // STOP/RESET
            if (swRunning) { // STOP
              swElapsedTime += millis() - swStartTime;
              swRunning = false;
            } else { // RESET
              swElapsedTime = 0;
            }
          }
        }
      }
      else if (currentScreen == SCREEN_STOPWATCH) {
      // Stopwatch Navigation
      if (buttonJustPressed[0]) { // UP
        if (swFocus == 1 || swFocus == 2) swFocus = 0; // Go up to back button
      }
      if (buttonJustPressed[1]) { // DOWN
        if (swFocus == 0) swFocus = 1; // Go down to start button
      }
      if (buttonJustPressed[2]) { // LEFT
        if (swFocus == 2) swFocus = 1; // From reset to start
      }
      if (buttonJustPressed[3]) { // RIGHT
        if (swFocus == 1) swFocus = 2; // From start to reset
      }
      
      if (buttonJustPressed[4]) { // CENTER (Click)
        if (swFocus == 0) { // Back
          startAnimation(SCREEN_MENU, 64);
        } else if (swFocus == 1) { // START/PAUSE
          if (swRunning) {
            swElapsedTime += millis() - swStartTime;
            swRunning = false;
          } else {
            swStartTime = millis();
            swRunning = true;
          }
        } else if (swFocus == 2) { // STOP/RESET
          if (swRunning) { // STOP
            swElapsedTime += millis() - swStartTime;
            swRunning = false;
          } else { // RESET
            swElapsedTime = 0;
          }
        }
      }
    }
    else if (currentScreen == SCREEN_TIMER) {
        // Timer Header Navigation
        if (buttonJustPressed[0]) { // UP
          if (tmFocus > 0) tmFocus = 0; // Go to <--
        }
        if (buttonJustPressed[1]) { // DOWN
          if (tmFocus == 0) tmFocus = 1; // From <-- to [+]
        }
        if (buttonJustPressed[2]) { // LEFT
          if (tmMode == TM_SETTING) {
            if (tmFocus == 2) tmFocus = 1; // From SET to +
            else if (tmFocus == 3) tmFocus = 2; // From - to SET
            else if (tmFocus == 1) tmFocus = 0; // From + to <--
          } else if (tmMode == TM_READY) {
            if (tmFocus == 3) tmFocus = 1; // From START back to SET
            else if (tmFocus == 1) tmFocus = 0; // From SET back to <--
          }
        }
        if (buttonJustPressed[3]) { // RIGHT
          if (tmMode == TM_SETTING) {
            if (tmFocus == 1) tmFocus = 2; // From + to SET
            else if (tmFocus == 2) tmFocus = 3; // From SET to -
          } else if (tmMode == TM_READY) {
            if (tmFocus == 1) tmFocus = 3; // From SET to START
          }
        }

        // Handle Long Press Detection for SET (Center Button while focus is SET)
        if (buttonStates[4] && tmFocus == 2 && tmMode == TM_SETTING) {
          if (tmSetPressStart == 0) tmSetPressStart = millis();
          if (millis() - tmSetPressStart > 2000 && !tmLongPressTriggered) {
            tmMode = TM_READY; // Switch to Ready mode
            tmFocus = 3; // Default focus to [START]
            tmLongPressTriggered = true;
            // Haptic or visual feedback could be added here
          }
        } else {
          tmSetPressStart = 0;
          tmLongPressTriggered = false;
        }

        if (buttonJustPressed[4]) { // CENTER (Click)
          if (tmFocus == 0) { // Back
            startAnimation(SCREEN_MENU, 64);
          } 
          else if (tmMode == TM_SETTING) {
            if (tmFocus == 1) { // [+]
              if (tmActiveUnit == 0) tmHours = (tmHours + 1) % 24;
              else if (tmActiveUnit == 1) tmMinutes = (tmMinutes + 1) % 60;
              else if (tmActiveUnit == 2) tmSeconds = (tmSeconds + 1) % 60;
            } else if (tmFocus == 2) { // [SET]
              tmActiveUnit = (tmActiveUnit + 1) % 3;
            } else if (tmFocus == 3) { // [-]
              if (tmActiveUnit == 0) tmHours = (tmHours + 23) % 24;
              else if (tmActiveUnit == 1) tmMinutes = (tmMinutes + 59) % 60;
              else if (tmActiveUnit == 2) tmSeconds = (tmSeconds + 59) % 60;
            }
          }
          else if (tmMode == TM_READY) {
            if (tmFocus == 1) tmMode = TM_SETTING; // [SET] returns to setting
            else if (tmFocus == 3) { // [START]
              tmRemainingMillis = ((unsigned long)tmHours * 3600 + tmMinutes * 60 + tmSeconds) * 1000;
              if (tmRemainingMillis > 0) {
                tmMode = TM_RUNNING;
                tmLastTick = millis();
              }
            }
          }
          else if (tmMode == TM_RUNNING) {
            if (tmFocus == 1) tmMode = TM_READY; // [RESET]
            else if (tmFocus == 3) { 
              // Pause logic could go here, but user asked for START -> PAUSE/RESET
              // Assuming START becomes PAUSE and SET stays as SET (to go back)
            }
          }
        }
      }
      else if (currentScreen == SCREEN_TIMER_ALERT) {
        if (buttonJustPressed[0] || buttonJustPressed[1] || buttonJustPressed[2] || 
            buttonJustPressed[3] || buttonJustPressed[4]) {
          tmMode = TM_SETTING;
          startAnimation(SCREEN_WATCHFACE, 64); // Return to home
        }
      }
    }

    // Timer Background Logic
    if (tmMode == TM_RUNNING) {
      unsigned long now = millis();
      unsigned long delta = now - tmLastTick;
      if (tmRemainingMillis > delta) {
        tmRemainingMillis -= delta;
        tmLastTick = now;
      } else {
        tmRemainingMillis = 0;
        tmMode = TM_RINGING;
        currentScreen = SCREEN_TIMER_ALERT;
        if (!displayOn) {
          display.ssd1306_command(SSD1306_DISPLAYON);
          displayOn = true;
        }
        lastActivityTime = millis();
      }
    }

    // =========================================
    // RADIO APP NAVIGATION
    // =========================================
    if (currentScreen == SCREEN_RADIO) {
      if (buttonJustPressed[0]) { if (radioCursor > -1) radioCursor--; } // Allow going up to -1 (the <-- button)
      if (buttonJustPressed[1]) { if (radioCursor < 3) radioCursor++; }
      if (buttonJustPressed[2]) {
        if (radioCursor == -1) radioCursor = 0; // LEFT moves off back button
      }
      if (buttonJustPressed[4]) { // CENTER
        if (radioCursor == -1) {
          startAnimation(SCREEN_MENU, 64);
        }
        else if (radioCursor == 0) {
          if (WiFi.status() == WL_CONNECTED) { wifiHomeCursor = 0; startAnimation(SCREEN_WIFI_HOME, -64); }
          else { wifiConfirmCursor = 0; startAnimation(SCREEN_RADIO_WIFI, -64); }
        } else if (radioCursor == 1) startAnimation(SCREEN_RADIO_BLE, -64);
        else if (radioCursor == 2) startAnimation(SCREEN_RADIO_ESPNOW, -64);
        else if (radioCursor == 3) startAnimation(SCREEN_RADIO_POWER, -64);
      }
    }    else if (currentScreen == SCREEN_RADIO_WIFI) {
      if (buttonJustPressed[0]) wifiConfirmCursor = -1; // UP = back
      if (buttonJustPressed[1]) { if (wifiConfirmCursor == -1) wifiConfirmCursor = 0; } // DOWN = YES
      if (buttonJustPressed[3]) wifiConfirmCursor = 1; // RIGHT = NO
      if (buttonJustPressed[2]) { if (wifiConfirmCursor == 1) wifiConfirmCursor = 0; }
      if (buttonJustPressed[4]) {
        if (wifiConfirmCursor == 0) {
          WiFi.disconnect(); // Ensure clean state
          WiFi.mode(WIFI_STA);
          // 250ms per channel * 14 channels ≈ 3.5 seconds
          WiFi.scanNetworks(true, false, false, 250);
          wifiScanStart = millis();
          wifiScanResults = -1;
          startAnimation(SCREEN_WIFI_SCANNING, -64);
        } else if (wifiConfirmCursor == 1) {
          startAnimation(SCREEN_RADIO, 64);
        } else {
          startAnimation(SCREEN_RADIO, 64);
        }
      }
    }

    else if (currentScreen == SCREEN_WIFI_RESULTS) {
      if (buttonJustPressed[0]) { if (wifiListIndex > -1) wifiListIndex--; }
      if (buttonJustPressed[1]) { if (wifiListIndex < wifiScanResults - 1) wifiListIndex++; }
      if (buttonJustPressed[2]) { if (wifiListIndex == -1) wifiListIndex = 0; }
      if (buttonJustPressed[4]) {
        if (wifiListIndex == -1) { startAnimation(SCREEN_RADIO, 64); }
        else {
          wifiTargetSSID = WiFi.SSID(wifiListIndex);
          wifiPassword   = findSavedPwd(wifiTargetSSID);
          kbInputBuffer  = wifiPassword;
          wifiStatusMsg  = "";
          wifiRetryCount = 0; // Reset for new network selection
          wifiPasswordCursor = -1; // Default to <-- on enter
          startAnimation(SCREEN_WIFI_PASSWORD, -64);
        }
      }
    }
    else if (currentScreen == SCREEN_WIFI_PASSWORD) {
      // NAV GRAPH: <-- (up) <=> Keyboard (down/left) <=> Connect (right)
      if (buttonJustPressed[0]) { 
        if (wifiPasswordCursor == 0 || wifiPasswordCursor == 1) wifiPasswordCursor = -1; 
      }
      if (buttonJustPressed[1]) { 
        if (wifiPasswordCursor == -1) wifiPasswordCursor = 0; 
      }
      if (buttonJustPressed[2]) { 
        if (wifiPasswordCursor == 1) wifiPasswordCursor = 0; 
      }
      if (buttonJustPressed[3]) {
        if (wifiPasswordCursor == 0) wifiPasswordCursor = 1;
      }
      if (buttonJustPressed[4]) {
        if (wifiPasswordCursor == -1) startAnimation(SCREEN_WIFI_RESULTS, 64);
        else if (wifiPasswordCursor == 0) {
          kbInputBuffer = wifiPassword; currentKBMode = KB_LOWER;
          kbCursorRow = 0; kbCursorCol = 0;
          kbReturnScreen = SCREEN_WIFI_PASSWORD;
          currentScreen = SCREEN_WIFI_KEYBOARD;
        } else if (wifiPasswordCursor == 1) {
          wifiStatusMsg = "Connecting...";
          wifiConnecting = true;
          wifiConnectStartTime = millis();
          WiFi.begin(wifiTargetSSID.c_str(), wifiPassword.c_str());
        }
      }
      
      if (wifiConnecting) {
        lastActivityTime = millis(); // keep screen on!
        if (WiFi.status() == WL_CONNECTED) {
          wifiConnecting = false;
          wifiRetryCount = 0;
          saveCredential(wifiTargetSSID, wifiPassword);
          syncTimeWithNTP();
          wifiHomeCursor = 0;
          startAnimation(SCREEN_WIFI_HOME, -64);
          lastActivityTime = millis(); // refresh timeout after connect
        } else if (WiFi.status() == WL_CONNECT_FAILED) {
          wifiConnecting = false;
          wifiRetryCount++;
          if (wifiRetryCount >= 5) wifiStatusMsg = "Check signal/pass";
          else wifiStatusMsg = "Failed! Wrong PW?";
        } else if (millis() - wifiConnectStartTime > 6000) {
          // 6-second timeout
          wifiConnecting = false;
          wifiRetryCount++;
          WiFi.disconnect();
          if (wifiRetryCount >= 5) wifiStatusMsg = "Check Router distance";
          else wifiStatusMsg = "Try Again...";
        }
      }
    }
    else if (currentScreen == SCREEN_WIFI_KEYBOARD) {
      if (buttonJustPressed[0]) { if (kbCursorRow > 0) kbCursorRow--; }
      if (buttonJustPressed[1]) { if (kbCursorRow < 3) kbCursorRow++; } // 4 rows
      if (buttonJustPressed[2]) { if (kbCursorCol > 0) kbCursorCol--; else { kbCursorCol = 10; if (kbCursorRow > 0) kbCursorRow--; } }
      if (buttonJustPressed[3]) { if (kbCursorCol < 10) kbCursorCol++; else { kbCursorCol = 0; if (kbCursorRow < 3) kbCursorRow++; } }
      if (buttonJustPressed[4]) {
        const char* key;
        if (currentKBMode == KB_UPPER) key = kbUpper[kbCursorRow][kbCursorCol];
        else if (currentKBMode == KB_SYMBOL) key = kbSymbol[kbCursorRow][kbCursorCol];
        else key = kbLower[kbCursorRow][kbCursorCol];
        
        if (strcmp(key, "EN") == 0) { wifiPassword = kbInputBuffer; currentScreen = kbReturnScreen; }
        else if (strcmp(key, "<") == 0) { if (kbInputBuffer.length() > 0) kbInputBuffer.remove(kbInputBuffer.length() - 1); }
        else if (strcmp(key, "^") == 0) {
          if (currentKBMode == KB_LOWER) currentKBMode = KB_UPPER;
          else if (currentKBMode == KB_UPPER) currentKBMode = KB_SYMBOL;
          else currentKBMode = KB_LOWER;
        }
        else kbInputBuffer += String(key);
      }
    }
    else if (currentScreen == SCREEN_WIFI_HOME) {
      if (buttonJustPressed[0]) { if (wifiHomeCursor > -1) wifiHomeCursor--; }
      if (buttonJustPressed[1]) { if (wifiHomeCursor < 2) wifiHomeCursor++; }
      if (buttonJustPressed[2]) { if (wifiHomeCursor == -1) wifiHomeCursor = 0; }
      if (buttonJustPressed[4]) {
        if (wifiHomeCursor == -1) startAnimation(SCREEN_RADIO, 64);
        else if (wifiHomeCursor == 0) { wifiToggleCursor = 1; startAnimation(SCREEN_WIFI_TOGGLE_CONFIRM, -64); }
        else if (wifiHomeCursor == 1) { 
          if (WiFi.status() == WL_CONNECTED) { 
            ntpSyncStart = millis(); 
            ntpSyncState = 0;
            configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
            startAnimation(SCREEN_WIFI_NTP, -64);
          } 
        }
        else if (wifiHomeCursor == 2) { forgetCredential(WiFi.SSID()); WiFi.disconnect(); startAnimation(SCREEN_RADIO, 64); }
      }
    }
    else if (currentScreen == SCREEN_WIFI_TOGGLE_CONFIRM) {
      if (buttonJustPressed[2]) wifiToggleCursor = 0;
      if (buttonJustPressed[3]) wifiToggleCursor = 1;
      if (buttonJustPressed[4]) {
        if (wifiToggleCursor == 0) { WiFi.disconnect(); WiFi.mode(WIFI_OFF); }
        startAnimation(SCREEN_RADIO, 64);
      }
    }
    else if (currentScreen == SCREEN_RADIO_BLE || currentScreen == SCREEN_RADIO_ESPNOW || currentScreen == SCREEN_RADIO_POWER) {
      if (buttonJustPressed[4]) startAnimation(SCREEN_RADIO, 64);
    }

    // Update status animation index periodically
    static unsigned long lastStatusUpdate = 0;
    if (millis() - lastStatusUpdate > 100) {
      statusIndex++;
      lastStatusUpdate = millis();
    }

    updateDisplay();
  }
  
  delay(5); // Fast UI loop
}
void startAnimation(ScreenState next, int targetOffset) {
  isAnimating = true;
  animNextScreen = next;
  animOffsetY = 0;
  animTargetY = targetOffset;
}

void updateDisplay() {
  display.clearDisplay();
  
  if (isAnimating) {
    // Determine screen pair based on targetOffset
    if (animTargetY < 0) { // Sliding up
      drawScreen(currentScreen, animOffsetY); // Moves up (negative offset)
      drawScreen(animNextScreen, animOffsetY + 64); // Comes from below
    } else { // Sliding down
      drawScreen(currentScreen, animOffsetY); // Moves down (positive offset)
      drawScreen(animNextScreen, animOffsetY - 64); // Comes from above
    }
  } else {
    drawScreen(currentScreen, 0);
  }
  
  display.display();
}

void drawScreen(ScreenState screen, int yOffset) {
  if (screen == SCREEN_WATCHFACE) drawWatchFace(yOffset);
  else if (screen == SCREEN_MENU) drawMenu(yOffset);
  else if (screen == SCREEN_STOPWATCH) drawStopwatch(yOffset);
  else if (screen == SCREEN_TIMER) drawTimer(yOffset);
  else if (screen == SCREEN_TIMER_ALERT) drawTimerAlert(yOffset);
  else if (screen == SCREEN_RADIO) drawRadioHome(yOffset);
  else if (screen == SCREEN_RADIO_WIFI) drawRadioWifi(yOffset);
  else if (screen == SCREEN_WIFI_CONFIRM) drawWifiConfirm(yOffset);
  else if (screen == SCREEN_WIFI_SCANNING) drawWifiScanning(yOffset);
  else if (screen == SCREEN_WIFI_RESULTS) drawWifiResults(yOffset);
  else if (screen == SCREEN_WIFI_PASSWORD) drawWifiPassword(yOffset);
  else if (screen == SCREEN_WIFI_KEYBOARD) drawWifiKeyboard();
  else if (screen == SCREEN_WIFI_HOME) drawWifiHome(yOffset);
  else if (screen == SCREEN_WIFI_TOGGLE_CONFIRM) drawWifiToggleConfirm(yOffset);
  else if (screen == SCREEN_RADIO_BLE) drawRadioStub(yOffset, "Bluetooth");
  else if (screen == SCREEN_RADIO_ESPNOW) drawRadioStub(yOffset, "ESP-NOW");
  else if (screen == SCREEN_RADIO_POWER) drawRadioStub(yOffset, "Power Mode");
  else if (screen == SCREEN_WIFI_NTP) drawWifiNTP(yOffset);
}

void drawHeader(int yOffset, const char* appName, bool backFocused) {
  // Always draw the status bar separating line at the top
  display.drawFastHLine(0, 12 + yOffset, 128, SSD1306_WHITE);

  // Draw WiFi icon if connected (only on Home and Menu)
  if (WiFi.status() == WL_CONNECTED && (currentScreen == SCREEN_WATCHFACE || currentScreen == SCREEN_MENU)) {
    int wx = (appName == nullptr) ? 4 : 28; // Left aligned (after back arrow if present)
    int wy = 2 + yOffset;
    display.drawPixel(wx+3, wy+6, SSD1306_WHITE);
    display.drawPixel(wx+4, wy+6, SSD1306_WHITE);
    display.drawPixel(wx+2, wy+4, SSD1306_WHITE);
    display.drawPixel(wx+3, wy+3, SSD1306_WHITE);
    display.drawPixel(wx+4, wy+3, SSD1306_WHITE);
    display.drawPixel(wx+5, wy+4, SSD1306_WHITE);
    display.drawPixel(wx+0, wy+2, SSD1306_WHITE);
    display.drawPixel(wx+1, wy+1, SSD1306_WHITE);
    display.drawPixel(wx+2, wy+0, SSD1306_WHITE);
    display.drawPixel(wx+5, wy+0, SSD1306_WHITE);
    display.drawPixel(wx+6, wy+1, SSD1306_WHITE);
    display.drawPixel(wx+7, wy+2, SSD1306_WHITE);
    
    // Draw Internet signal bars icon if internet is OK (right next to WiFi)
    if (internetOK) {
      int tx = wx + 11; // 11px to the right of wifi
      int ty = 2 + yOffset;
      display.drawFastVLine(tx + 0, ty + 6, 2, SSD1306_WHITE);
      display.drawFastVLine(tx + 2, ty + 4, 4, SSD1306_WHITE);
      display.drawFastVLine(tx + 4, ty + 2, 6, SSD1306_WHITE);
      display.drawFastVLine(tx + 6, ty + 0, 8, SSD1306_WHITE);
    }
  }

  if (appName != nullptr) {
    display.setTextSize(1);
    
    // Left arrow highlight
    if (backFocused) {
      display.fillRect(0, yOffset, 24, 12, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    
    // Left arrow <--
    display.setCursor(2, 2 + yOffset);
    display.print("<--");
    
    // App Name
    display.setTextColor(SSD1306_WHITE);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(appName, 0, 0, &x1, &y1, &w, &h);
    // Vertical center in 0-12 area: (12-h)/2 - y1
    display.setCursor((128 - w) / 2 - x1, 12/2 - h/2 - y1 + yOffset);
    display.print(appName);
  } else {
    display.setTextColor(SSD1306_WHITE);
    if (isStatusActive) {
      uint8_t dotPos = (statusIndex % 8) * 16;
      display.fillRect(dotPos, 2 + yOffset, 8, 4, SSD1306_WHITE);
    }
  }
}

void drawWatchFace(int yOffset) {
  // Draw top line (status separator) like cyberdeck at y=12
  drawHeader(yOffset);
  
  DateTime now = rtc.now();
  uint16_t hour = now.hour();
  bool isPM = hour >= 12;
  if (hour > 12) hour -= 12;
  else if (hour == 0) hour = 12;
  
  // Cyberdeck clock formatting
  char timeBuf[6];
  sprintf(timeBuf, "%02d:%02d", hour, now.minute());
  display.setTextSize(4);
  display.setTextColor(SSD1306_WHITE);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(timeBuf, 0, 0, &x1, &y1, &w, &h);
  
  // Precise centering math for visual symmetry in the 12-56 region (height 44)
  // Available Area: y=13 to y=55 (43 pixels)
  int16_t xPos = (128 - w + 1) / 2 - x1;
  int16_t yPos = 13 + (43 - h + 1) / 2 - y1 + yOffset; 
  display.setCursor(xPos, yPos);
  display.print(timeBuf);
  
  // Cyberdeck AM/PM formatting
  display.setTextSize(1);
  display.setCursor(128 - 14, 56 + yOffset);
  display.print(isPM ? "PM" : "AM");
  
  // Cyberdeck Date formatting
  display.setCursor(0, 56 + yOffset);
  const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  char dateBuf[16];
  sprintf(dateBuf, "%02d/%02d/%04d %s", now.day(), now.month(), now.year(), days[now.dayOfTheWeek()]);
  display.print(dateBuf);
}

void drawMenu(int yOffset) {
  drawHeader(yOffset);
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  int itemH = 12;
  int listY = 16 + yOffset;

  
  for (int i = 0; i < NUM_MENU_ITEMS; i++) {
    int y = listY + (i * itemH);
    
    if (i == menuIndex) {
      display.fillRect(0, y-1, 128, itemH, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    
    display.setCursor(4, y);
    display.print(menuItems[i]);
  }
}

void drawStopwatch(int yOffset) {
  drawHeader(yOffset, "STOPWATCH", (swFocus == 0));
  
  // Back arrow is now handled by drawHeader
  
  // Calculate display time
  unsigned long currentTotal = swElapsedTime;
  if (swRunning) {
    currentTotal += millis() - swStartTime;
  }
  
  unsigned long ms = (currentTotal % 1000) / 10; // 0-99
  unsigned long totalSecs = currentTotal / 1000;
  unsigned long m = totalSecs / 60;
  unsigned long s = totalSecs % 60;
  
  // MM:SS centered, size 3
  char timeStr[10];
  sprintf(timeStr, "%02lu:%02lu", m, s);
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  int16_t xPos = (SCREEN_WIDTH - w) / 2 - x1;
  int16_t yPos = 10 + (42 - h) / 2 - y1 + yOffset;
  display.setCursor(xPos, yPos);
  display.print(timeStr);
  
  // Small MS moved down to avoid separator overlap
  char msStr[4];
  sprintf(msStr, "%02lu", ms);
  display.setTextSize(1);
  display.setCursor(SCREEN_WIDTH - 16, 16 + yOffset);
  display.print(msStr);
  
  // Bottom buttons with centered focus boxes
  display.setTextSize(1);
  
  // START/PAUSE (left side)
  const char* leftBtn = swRunning ? "PAUSE" : "START";
  int16_t lx1, ly1; uint16_t lw, lh;
  display.getTextBounds(leftBtn, 0, 0, &lx1, &ly1, &lw, &lh);
  int leftBtnX = (64 - (lw + 8)) / 2; 
  drawBoxedCenteredText(display, leftBtn, leftBtnX, SCREEN_HEIGHT - 12 + yOffset, lw + 8, 11, (swFocus == 1));
  
  // STOP/RESET (right side)
  const char* rightBtn = swRunning ? "STOP" : "RESET";
  int16_t rx1, ry1; uint16_t rw, rh;
  display.getTextBounds(rightBtn, 0, 0, &rx1, &ry1, &rw, &rh);
  int rightBtnX = 64 + (64 - (rw + 8)) / 2;
  drawBoxedCenteredText(display, rightBtn, rightBtnX, SCREEN_HEIGHT - 12 + yOffset, rw + 8, 11, (swFocus == 2));
}

void drawTimer(int yOffset) {
  drawHeader(yOffset, "TIMER", (tmFocus == 0));
  
  char timeStr[10];
  sprintf(timeStr, "%02d:%02d:%02d", tmHours, tmMinutes, tmSeconds);
  if (tmMode == TM_RUNNING) {
    unsigned long secs = tmRemainingMillis / 1000;
    sprintf(timeStr, "%02lu:%02lu:%02lu", secs / 3600, (secs % 3600) / 60, secs % 60);
  }

  // Main display: HH:MM:SS centered in upper content area
  display.setTextSize(2);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  int16_t tx = (128 - w) / 2 - x1;
  int16_t ty = 16 + (28 - h) / 2 - y1 + yOffset;
  
  display.setCursor(tx, ty);
  display.print(timeStr);

  // Indicators for which unit is being edited
  if (tmMode == TM_SETTING) {
    int charW = 12; // approx spacing for size 2
    int16_t unitX = tx + (tmActiveUnit * 3 * charW); 
    display.drawFastHLine(unitX, ty + h + 2, 22, SSD1306_WHITE);
  }

  // Draw buttons in bottom area
  if (tmMode == TM_SETTING) {
    // [+] [SET] [-]
    drawBoxedCenteredText(display, "+", 10, 48 + yOffset, 20, 11, (tmFocus == 1));
    drawBoxedCenteredText(display, "SET", 44, 48 + yOffset, 40, 11, (tmFocus == 2));
    drawBoxedCenteredText(display, "-", 98, 48 + yOffset, 20, 11, (tmFocus == 3));
  } else if (tmMode == TM_READY || tmMode == TM_RUNNING) {
    // [SET] [START/PAUSE]
    const char* mainBtn = (tmMode == TM_RUNNING) ? "RESET" : "START";
    drawBoxedCenteredText(display, "SET", 15, 48 + yOffset, 40, 11, (tmFocus == 1));
    drawBoxedCenteredText(display, mainBtn, 70, 48 + yOffset, 45, 11, (tmFocus == 3));
  }
}

void drawTimerAlert(int yOffset) {
  bool isFlashOn = (millis() % 1000 < 500);
  
  if (isFlashOn) {
    // Inverted: White background, Black text
    display.fillRect(0, 0, 128, 64, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    // Normal: Black background, White text
    display.setTextColor(SSD1306_WHITE);
  }

  drawCenteredText(display, "TIMER DONE", 15 + yOffset, 2);
  drawCenteredText(display, "Press any key", 40 + yOffset, 1);
  
  // Custom OK button that inverts relative to flash state
  int x = 51, y = 51 + yOffset, w = 26, h = 11;
  if (isFlashOn) {
    // Background is white, so draw a black box with white text
    display.fillRect(x, y, w, h, SSD1306_BLACK);
    display.setTextColor(SSD1306_WHITE);
  } else {
    // Background is black, so draw a white box with black text
    display.fillRect(x, y, w, h, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  }
  
  // Center "OK" in the box manually for precision
  int16_t x1, y1; uint16_t tw, th;
  display.getTextBounds("OK", 0, 0, &x1, &y1, &tw, &th);
  display.setCursor(x + (w - tw) / 2 - x1, y + (h - th) / 2 - y1);
  display.print("OK");
  
  display.setTextColor(SSD1306_WHITE); // Reset for other draw calls
}



// FreeRTOS task for reading button inputs
void buttonReadTask(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(50);  // 50ms interval
  
  while (1) {
    // Read all button states (LOW = pressed, HIGH = not pressed)
    buttonStates[0] = !digitalRead(BTN_UP);     // Button 6 (UP)
    buttonStates[1] = !digitalRead(BTN_DOWN);   // Button 9 (DOWN)
    buttonStates[2] = !digitalRead(BTN_LEFT);   // Button 8 (LEFT)
    buttonStates[3] = !digitalRead(BTN_RIGHT);  // Button 7 (RIGHT)
    buttonStates[4] = !digitalRead(BTN_CENTER); // Button 10 (CENTER)
    
    // vTaskDelayUntil handles the interval
    
    // Delay for debouncing
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// Print button states in format: 6: NO 9: NO ...
// vTaskDelayUntil used in task

// Helper function to draw centered text
void drawCenteredText(Adafruit_SSD1306 &d, const String &text, int16_t y, uint8_t size) {
  int16_t x1, y1; uint16_t w, h;
  d.setTextSize(size);
  d.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  d.setCursor((SCREEN_WIDTH - w) / 2, y);
  d.print(text);
}

// Draw text centered in a box
void drawBoxedCenteredText(Adafruit_SSD1306 &d, const char* text, int x, int y, int w, int h, bool inverted) {
  int16_t x1, y1; uint16_t tw, th;
  d.setTextSize(1);
  d.getTextBounds(text, 0, 0, &x1, &y1, &tw, &th);
  if (inverted) {
    d.fillRect(x, y, w, h, SSD1306_WHITE);
    d.setTextColor(SSD1306_BLACK);
  } else {
    d.drawRect(x, y, w, h, SSD1306_WHITE);
    d.setTextColor(SSD1306_WHITE);
  }
  d.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
  d.print(text);
  d.setTextColor(SSD1306_WHITE);
}

// ============================================================
// RADIO APP HELPERS
// ============================================================

void loadCredentials() {
  prefs.begin("wifi_creds", true);
  savedNetCount = prefs.getInt("count", 0);
  for (int i = 0; i < savedNetCount && i < MAX_SAVED_NETS; i++) {
    savedSSIDs[i] = prefs.getString(("ssid" + String(i)).c_str(), "");
    savedPWDs[i]  = prefs.getString(("pwd"  + String(i)).c_str(), "");
  }
  prefs.end();
}

void saveCredential(String ssid, String pwd) {
  // Check if already saved
  for (int i = 0; i < savedNetCount; i++) {
    if (savedSSIDs[i] == ssid) { savedPWDs[i] = pwd; goto writeAll; }
  }
  if (savedNetCount < MAX_SAVED_NETS) {
    savedSSIDs[savedNetCount] = ssid;
    savedPWDs[savedNetCount]  = pwd;
    savedNetCount++;
  }
  writeAll:
  prefs.begin("wifi_creds", false);
  prefs.putInt("count", savedNetCount);
  for (int i = 0; i < savedNetCount; i++) {
    prefs.putString(("ssid" + String(i)).c_str(), savedSSIDs[i].c_str());
    prefs.putString(("pwd"  + String(i)).c_str(), savedPWDs[i].c_str());
  }
  prefs.end();
}

void forgetCredential(String ssid) {
  int found = -1;
  for (int i = 0; i < savedNetCount; i++) {
    if (savedSSIDs[i] == ssid) { found = i; break; }
  }
  if (found == -1) return;
  for (int i = found; i < savedNetCount - 1; i++) {
    savedSSIDs[i] = savedSSIDs[i+1];
    savedPWDs[i]  = savedPWDs[i+1];
  }
  savedNetCount--;
  prefs.begin("wifi_creds", false);
  prefs.putInt("count", savedNetCount);
  for (int i = 0; i < savedNetCount; i++) {
    prefs.putString(("ssid" + String(i)).c_str(), savedSSIDs[i].c_str());
    prefs.putString(("pwd"  + String(i)).c_str(), savedPWDs[i].c_str());
  }
  prefs.end();
}

String findSavedPwd(String ssid) {
  for (int i = 0; i < savedNetCount; i++) {
    if (savedSSIDs[i] == ssid) return savedPWDs[i];
  }
  return "";
}

void syncTimeWithNTP() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); // async
}

void internetPinger1(void *p) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient h; h.setConnectTimeout(1200); h.setTimeout(1800);
      h.begin("http://www.gstatic.com/generate_204");
      pingerStatus[0] = (h.GET() == 204); h.end();
    } else pingerStatus[0] = false;
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
void internetPinger2(void *p) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient h; h.setConnectTimeout(1200); h.setTimeout(1800);
      h.begin("http://clients3.google.com/generate_204");
      pingerStatus[1] = (h.GET() == 204); h.end();
    } else pingerStatus[1] = false;
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
void internetPinger3(void *p) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient h; h.setConnectTimeout(1200); h.setTimeout(1800);
      h.begin("http://cp.cloudflare.com/generate_204");
      pingerStatus[2] = (h.GET() == 204); h.end();
    } else pingerStatus[2] = false;
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
void internetPinger4(void *p) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient h; h.setConnectTimeout(1200); h.setTimeout(1800);
      h.begin("http://edge-http.microsoft.com/captiveportal/generate_204");
      pingerStatus[3] = (h.GET() == 204); h.end();
    } else pingerStatus[3] = false;
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
void internetPinger5(void *p) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient h; h.setConnectTimeout(1200); h.setTimeout(1800);
      h.begin("http://connect.rom.miui.com/generate_204");
      pingerStatus[4] = (h.GET() == 204); h.end();
    } else pingerStatus[4] = false;
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
void connectivityAggregator(void *p) {
  for (;;) {
    bool prev = internetOK;
    internetOK = pingerStatus[0] || pingerStatus[1] || pingerStatus[2] || pingerStatus[3] || pingerStatus[4];
    if (prev != internetOK) statusChanged = true;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void startWifiPingers() {
  xTaskCreate(internetPinger1, "P1", 3072, NULL, 1, NULL);
  xTaskCreate(internetPinger2, "P2", 3072, NULL, 1, NULL);
  xTaskCreate(internetPinger3, "P3", 3072, NULL, 1, NULL);
  xTaskCreate(internetPinger4, "P4", 3072, NULL, 1, NULL);
  xTaskCreate(internetPinger5, "P5", 3072, NULL, 1, NULL);
  xTaskCreate(connectivityAggregator, "NetAgg", 2048, NULL, 2, NULL);
}

// ============================================================
// RADIO DRAW FUNCTIONS
// ============================================================

void drawRadioHome(int yOffset) {
  drawHeader(yOffset, "RADIO", (radioCursor == -1));
  const char* opts[] = {"WiFi", "Bluetooth", "ESP-NOW", "Power Mode"};
  for (int i = 0; i < 4; i++) {
    int y = 16 + (i * 12) + yOffset;
    bool sel = (radioCursor == i);
    if (sel) {
      display.fillRect(0, y - 1, 128, 12, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(4, y);
    display.print(opts[i]);
  }
  display.setTextColor(SSD1306_WHITE);
}

void drawRadioWifi(int yOffset) {
  drawHeader(yOffset, "WiFi", (wifiConfirmCursor == -1)); 
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText(display, "WiFi is OFF", 22 + yOffset, 1);
  drawCenteredText(display, "Turn it on?", 33 + yOffset, 1);
  // YES / NO
  drawBoxedCenteredText(display, "YES", 14, 48 + yOffset, 40, 13, (wifiConfirmCursor == 0));
  drawBoxedCenteredText(display, "NO",  74, 48 + yOffset, 40, 13, (wifiConfirmCursor == 1));
}

void drawWifiConfirm(int yOffset) {
  drawRadioWifi(yOffset);
}

void drawWifiScanning(int yOffset) {
  drawHeader(yOffset, "WiFi");
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText(display, "SCANNING...", 30 + yOffset, 1);
  // Animated dots
  int dots = ((millis() - wifiScanStart) / 500) % 4;
  String dotStr = "";
  for (int i = 0; i < dots; i++) dotStr += ".";
  drawCenteredText(display, dotStr, 42 + yOffset, 1);
}

void drawWifiResults(int yOffset) {
  drawHeader(yOffset, "WiFi", (wifiListIndex == -1));
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  if (wifiScanResults <= 0) {
    drawCenteredText(display, "No networks found", 30 + yOffset, 1);
    return;
  }
  // Show up to 4 results with scrolling
  int visibleItems = 4;
  if (wifiListIndex > -1 && wifiListIndex < wifiListScroll) wifiListScroll = wifiListIndex;
  if (wifiListIndex >= wifiListScroll + visibleItems) wifiListScroll = wifiListIndex - visibleItems + 1;
  for (int i = 0; i < visibleItems; i++) {
    int netIdx = wifiListScroll + i;
    if (netIdx >= wifiScanResults) break;
    int y = 16 + (i * 12) + yOffset;
    bool selected = (wifiListIndex == netIdx);
    if (selected) {
      display.fillRect(0, y - 1, 124, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(4, y);
    String ssid = WiFi.SSID(netIdx);
    if (ssid.length() > 18) ssid = ssid.substring(0, 17) + "~";
    display.print(ssid);
  }
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(true);
  // Scroll indicator
  if (wifiScanResults > visibleItems) {
    int sbH = (visibleItems * 48) / wifiScanResults;
    int sbY = 16 + (wifiListScroll * (48 - sbH) / (wifiScanResults - visibleItems));
    display.drawFastVLine(126, 16 + yOffset, 48, SSD1306_WHITE);
    display.fillRect(125, sbY + yOffset, 3, sbH, SSD1306_WHITE);
  }
}

void drawWifiPassword(int yOffset) {
  drawHeader(yOffset, "WiFi", (wifiPasswordCursor == -1));
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  // SSID truncated
  display.setCursor(4, 16 + yOffset);
  String ssid = wifiTargetSSID;
  if (ssid.length() > 18) ssid = ssid.substring(0, 17) + "~";
  display.print(ssid);
  display.setTextWrap(true);
  // Password box
  display.drawRect(4, 25 + yOffset, 120, 13, SSD1306_WHITE);
  display.setCursor(8, 28 + yOffset);
  String pw = wifiPassword;
  if (pw.length() > 15) pw = pw.substring(pw.length() - 15);
  if (pw.length() == 0) { display.setTextColor(0xAAAA); display.print("[tap to enter]"); }
  else display.print(pw);
  display.setTextColor(SSD1306_WHITE);
  // Status message
  if (wifiStatusMsg.length() > 0) {
    display.setCursor(4, 40 + yOffset);
    display.print(wifiStatusMsg);
  }
  // Buttons
  drawBoxedCenteredText(display, "KEYBOARD", 4,  51 + yOffset, 54, 12, (wifiPasswordCursor == 0));
  drawBoxedCenteredText(display, "CONNECT",  62, 51 + yOffset, 60, 12, (wifiPasswordCursor == 1));
}

void drawWifiKeyboard() {
  // Input bar (y=0..12)
  display.setTextColor(SSD1306_WHITE);
  display.drawRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setCursor(4, 3);
  String disp = kbInputBuffer;
  // Show last 17 chars
  if (disp.length() > 17) disp = disp.substring(disp.length() - 17);
  if (disp.length() == 0) { display.setTextColor(0x5555); display.print("Type password..."); }
  else display.print(disp);
  display.setTextColor(SSD1306_WHITE);

  // Keyboard rows (liquid layout to fill strictly bottom 50px area constraints)
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 11; c++) {
      const char* key;
      if (currentKBMode == KB_UPPER) key = kbUpper[r][c];
      else if (currentKBMode == KB_SYMBOL) key = kbSymbol[r][c];
      else key = kbLower[r][c];

      // Exact pixel boundary distribution using rational fractions
      int x = (c * 128) / 11;
      int w = ((c + 1) * 128) / 11 - x;
      int y = 13 + (r * 51) / 4;
      int h = (13 + ((r + 1) * 51) / 4) - y;
      
      drawBoxedCenteredText(display, key, x, y, w, h, (r == kbCursorRow && c == kbCursorCol));
    }
  }
}

void drawWifiHome(int yOffset) {
  drawHeader(yOffset, "WiFi", (wifiHomeCursor == -1));
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  // Static lines
  display.setCursor(4, 16 + yOffset);
  display.print("Internet: "); display.print(internetOK ? "OK" : "OFFLINE");
  display.setCursor(4, 25 + yOffset);
  display.print("SSID: ");
  String ssid = WiFi.SSID();
  if (ssid.length() > 12) ssid = ssid.substring(0, 11) + "~";
  display.print(ssid);
  display.setTextWrap(true);
  // Interactive menu items
  const char* opts[] = {"WiFi: ON", "SYNC TIME", "FORGET NETWORK"};
  for (int i = 0; i < 3; i++) {
    int y = 36 + (i * 10) + yOffset;
    bool sel = (wifiHomeCursor == i);
    if (sel) {
      display.fillRect(0, y - 1, 128, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(4, y);
    display.print(opts[i]);
  }
  display.setTextColor(SSD1306_WHITE);
}

void drawWifiToggleConfirm(int yOffset) {
  drawHeader(yOffset, "WiFi");
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText(display, "Turn off WiFi?", 22 + yOffset, 1);
  drawBoxedCenteredText(display, "YES", 14, 48 + yOffset, 40, 13, (wifiToggleCursor == 0));
  drawBoxedCenteredText(display, "NO",  74, 48 + yOffset, 40, 13, (wifiToggleCursor == 1));
}

void drawWifiNTP(int yOffset) {
  drawHeader(yOffset, "WiFi NTP");
  display.setTextColor(SSD1306_WHITE);
  
  if (ntpSyncState == 0) {
    drawCenteredText(display, "Syncing time...", 30 + yOffset, 1);
    int dots = ((millis() - ntpSyncStart) / 500) % 4;
    String dotStr = "";
    for (int i = 0; i < dots; i++) dotStr += ".";
    drawCenteredText(display, dotStr, 42 + yOffset, 1);
  } else if (ntpSyncState == 1) {
    drawCenteredText(display, "NTP Sync Success!", 30 + yOffset, 1);
    char tBuf[16];
    DateTime now = rtc.now();
    sprintf(tBuf, "%02d:%02d", now.hour(), now.minute());
    drawCenteredText(display, tBuf, 42 + yOffset, 1);
  } else if (ntpSyncState == 2) {
    drawCenteredText(display, "NTP Sync Failed", 30 + yOffset, 1);
    drawCenteredText(display, "Timeout", 42 + yOffset, 1);
  }
}

void drawRadioStub(int yOffset, const char* name) {
  drawHeader(yOffset, name, true);
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText(display, name, 28 + yOffset, 1);
  drawCenteredText(display, "Coming Soon", 40 + yOffset, 1);
}

