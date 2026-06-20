/**
 * @file DCF77_Advanced_Example.ino
 * @brief Advanced DCF77 example with diagnostics and persistence
 * 
 * Demonstrates:
 * 1. Detailed state monitoring and error codes
 * 2. Signal quality feedback
 * 3. Threshold persistence (across resets)
 * 4. Power-efficient sync cycles
 * 5. Manual diagnostic commands via Serial
 */

#include "DCF77.h"
#include <Preferences.h>  // For threshold persistence (NVS)

#define PIN_DCF77_SIGNAL  6
#define PIN_DCF77_ENABLE  2
#define SERIAL_BAUD       115200

// ============================================================
// GLOBALS
// ============================================================

DCF77 dcf(PIN_DCF77_SIGNAL, PIN_DCF77_ENABLE);
Preferences prefs;  // For NVS persistence (ESP32 only)

unsigned long syncStartTime = 0;
unsigned long statusPrintTime = 0;
unsigned long lastSyncTime = 0;
int storedThreshold = DCF77_BIT_THRESHOLD_DEFAULT;

enum AppState {
  APP_IDLE,
  APP_CALIBRATING,
  APP_ACQUIRING,
  APP_LOCKED,
  APP_FAILED
} appState = APP_IDLE;

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);
  Serial.println("\n\n========================================");
  Serial.println("DCF77 Advanced Decoder Example");
  Serial.println("========================================\n");

  // Load stored threshold from NVS
  loadThreshold();
  Serial.printf("Stored threshold: %d ms\n\n", storedThreshold);

  // Initialize library
  if (!dcf.begin()) {
    Serial.println("ERROR: Failed to initialize DCF77!");
    while (1) delay(1000);
  }

  Serial.println("Commands:");
  Serial.println("  'c' - Calibrate threshold");
  Serial.println("  's' - Start sync");
  Serial.println("  'x' - Stop sync & disable module");
  Serial.println("  'q' - Query status");
  Serial.println("  'r' - Reset to defaults\n");

  appState = APP_IDLE;
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
  // Handle serial commands
  if (Serial.available()) {
    handleCommand(Serial.read());
  }

  // Process signal in polling mode
  if (appState == APP_ACQUIRING) {
    dcf.processSignal();
  }

  // Status reporting (5s interval)
  if (millis() - statusPrintTime >= 5000) {
    statusPrintTime = millis();
    printDetailedStatus();
  }

  // Check for lock
  if (dcf.getLockState() == LockState::LOCKED && appState == APP_ACQUIRING) {
    appState = APP_LOCKED;
    onLocked();
  }

  // Timeout (120s max)
  if (appState == APP_ACQUIRING &&
      millis() - syncStartTime > 120000) {
    Serial.println("\n[TIMEOUT] Acquisition took too long.");
    appState = APP_FAILED;
    dcf.stopAcquisition();
    dcf.disableModule();
  }

  delay(10);
}

// ============================================================
// COMMAND HANDLER
// ============================================================

void handleCommand(char cmd) {
  switch (cmd) {
    case 'c':
    case 'C':
      cmdCalibrate();
      break;
    case 's':
    case 'S':
      cmdStartSync();
      break;
    case 'x':
    case 'X':
      cmdStop();
      break;
    case 'q':
    case 'Q':
      printDetailedStatus();
      break;
    case 'r':
    case 'R':
      cmdReset();
      break;
    case '\r':
    case '\n':
      break;  // Ignore newlines
    default:
      Serial.printf("Unknown command: '%c'\n", cmd);
  }
}

void cmdCalibrate() {
  Serial.println("\n--- CALIBRATING (60 sec) ---");
  appState = APP_CALIBRATING;

  dcf.enableModule();
  delay(1000);

  if (dcf.calibrate()) {
    storedThreshold = dcf.getThreshold();
    saveThreshold();
    Serial.printf("✓ Calibration OK — threshold: %d ms\n", storedThreshold);
  } else {
    Serial.println("✗ Calibration failed, using stored value");
    dcf.setThreshold(storedThreshold);
  }

  appState = APP_IDLE;
  Serial.println("Type 's' to start sync.\n");
}

void cmdStartSync() {
  Serial.println("\n--- STARTING SYNC ---");
  appState = APP_ACQUIRING;
  syncStartTime = millis();

  // Use stored threshold
  dcf.setThreshold(storedThreshold);
  dcf.enableModule();
  delay(1000);

  dcf.startAcquisition();
  Serial.println("Listening for DCF77 signal (120 sec timeout)...\n");
}

void cmdStop() {
  Serial.println("\n--- STOPPING ---");
  appState = APP_IDLE;
  dcf.stopAcquisition();
  dcf.disableModule();
  Serial.println("Module disabled.\n");
}

void cmdReset() {
  Serial.println("\n--- RESET TO DEFAULTS ---");
  storedThreshold = DCF77_BIT_THRESHOLD_DEFAULT;
  saveThreshold();
  dcf.setThreshold(storedThreshold);
  Serial.printf("Threshold reset to: %d ms\n\n", storedThreshold);
}

// ============================================================
// STATUS PRINTER
// ============================================================

void printDetailedStatus() {
  Serial.printf("\n[%7lu ms] ", millis());

  // App state
  Serial.print("State: ");
  switch (appState) {
    case APP_IDLE:         Serial.print("IDLE      "); break;
    case APP_CALIBRATING:  Serial.print("CALIB     "); break;
    case APP_ACQUIRING:    Serial.print("ACQUIRE   "); break;
    case APP_LOCKED:       Serial.print("LOCKED    "); break;
    case APP_FAILED:       Serial.print("FAILED    "); break;
  }
  Serial.print(" | ");

  // Lock state
  Serial.print("Lock: ");
  switch (dcf.getLockState()) {
    case LockState::UNLOCKED: Serial.print("UNLOCKED "); break;
    case LockState::SYNCING:  Serial.print("SYNCING  "); break;
    case LockState::LOCKED:   Serial.print("LOCKED   "); break;
  }
  Serial.print(" | ");

  // Sync state
  Serial.print("Sync: ");
  switch (dcf.getSyncState()) {
    case SyncState::IDLE:      Serial.print("IDLE    "); break;
    case SyncState::ACQUIRING: Serial.print("ACQ     "); break;
    case SyncState::COMPLETE:  Serial.print("DONE    "); break;
    case SyncState::FAILED:    Serial.print("FAILED  "); break;
  }
  Serial.print("\n         ");

  // Signal quality bar
  int quality = dcf.getSignalQuality();
  Serial.print("Signal: [");
  int filled = quality / 10;
  for (int i = 0; i < 10; i++) {
    Serial.print(i < filled ? "█" : "░");
  }
  Serial.printf("] %3d%% | ", quality);

  // Frame stats
  Serial.printf("Good:%2d Bad:%2d | Bits:%2d\n",
               dcf.getGoodFrameCount(),
               dcf.getBadFrameCount(),
               dcf.getBitCount());

  // Threshold
  Serial.printf("         Threshold: %d ms | ", dcf.getThreshold());

  // Error
  if (dcf.getLastError() != DecodeError::NO_ERROR) {
    Serial.printf("Error: 0x%02X (", (uint8_t)dcf.getLastError());
    bool first = true;
    if (dcf.getLastError() == DecodeError::NO_ERROR)
      Serial.print("NONE");
    else {
      if (dcf.getLastError() & DecodeError::PARITY_MIN) {
        if (!first) Serial.print("|");
        Serial.print("MIN_PARITY");
        first = false;
      }
      if (dcf.getLastError() & DecodeError::PARITY_HOUR) {
        if (!first) Serial.print("|");
        Serial.print("HOUR_PARITY");
        first = false;
      }
      if (dcf.getLastError() & DecodeError::PARITY_DATE) {
        if (!first) Serial.print("|");
        Serial.print("DATE_PARITY");
        first = false;
      }
      if (dcf.getLastError() & DecodeError::SANITY_CHECK) {
        if (!first) Serial.print("|");
        Serial.print("SANITY");
        first = false;
      }
      if (dcf.getLastError() & DecodeError::FRAME_SHORT) {
        if (!first) Serial.print("|");
        Serial.print("SHORT");
        first = false;
      }
    }
    Serial.println(")");
  } else {
    Serial.println("Error: NONE");
  }
}

void onLocked() {
  dcf.disableModule();
  lastSyncTime = millis();

  const DCF77Time& t = dcf.decodedTime();

  Serial.println("\n" \
                 "════════════════════════════════════════\n");
  Serial.println("✓ LOCK ACHIEVED");
  Serial.println("════════════════════════════════════════");
  Serial.printf("Time:        %02d:%02d\n", t.hour, t.minute);
  Serial.printf("Date:        %02d/%02d/20%02d\n", t.day, t.month, t.year);
  Serial.printf("Day of week: %d (1=Mon, 7=Sun)\n", t.dow);
  Serial.printf("Timezone:    %s (UTC%+d)\n",
               t.cest ? "CEST" : "CET",
               t.cest ? 2 : 1);
  Serial.printf("Signal:      %d%%\n", dcf.getSignalQuality());
  Serial.printf("Good frames: %d\n", dcf.getGoodFrameCount());
  Serial.printf("Threshold:   %d ms\n", dcf.getThreshold());
  Serial.println("════════════════════════════════════════\n");

  dcf.stopAcquisition();

  Serial.println("Time is valid and ready for use.");
  Serial.println("Type 's' to start a new sync, 'c' to recalibrate.\n");
}

// ============================================================
// PERSISTENCE (NVS)
// ============================================================

void saveThreshold() {
  prefs.begin("dcf77", false);
  prefs.putInt("threshold", storedThreshold);
  prefs.end();
  Serial.printf("[NVS] Saved threshold: %d ms\n", storedThreshold);
}

void loadThreshold() {
  prefs.begin("dcf77", true);
  storedThreshold = prefs.getInt("threshold", DCF77_BIT_THRESHOLD_DEFAULT);
  prefs.end();
}

