/**
 * @file DCF77_Example.ino
 * @brief Simple DCF77 decoder example
 * 
 * Demonstrates both:
 * 1. **Polling mode** — call processSignal() in loop (lightweight)
 * 2. **Task mode** — background FreeRTOS task (better responsiveness)
 * 
 * Build for: ESP32-S3 Dev Module (or any ESP32 variant)
 * 
 * **Wiring:**
 * - DCF77 OUT → GPIO 6
 * - DCF77 EN  → GPIO 2  (optional; set to -1 if always on)
 * - DCF77 GND → GND
 * - DCF77 VCC → 5V or 3.3V (check module specs)
 */

#include "DCF77.h"

// ============================================================
// CONFIGURATION
// ============================================================

#define PIN_DCF77_SIGNAL  6    // GPIO connected to DCF77 module output
#define PIN_DCF77_ENABLE  2    // GPIO controlling module power (-1 if always on)
#define SERIAL_BAUD       115200

// Choose ONE of these modes:
#define USE_POLLING_MODE  1    // 1 = poll in loop, 0 = background task
#define USE_TASK_MODE     0    // 1 = FreeRTOS task on Core 0, 0 = polling only

// ============================================================
// GLOBALS
// ============================================================

DCF77 dcf(PIN_DCF77_SIGNAL, PIN_DCF77_ENABLE);
unsigned long syncStartTime = 0;
unsigned long statusPrintTime = 0;

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);
  Serial.println("\n========================================");
  Serial.println("DCF77 Decoder Example");
  Serial.println("========================================\n");

  // Initialize library
  if (!dcf.begin()) {
    Serial.println("ERROR: Failed to initialize DCF77!");
    while (1) delay(1000);
  }

  Serial.println("Initializing DCF77...");

  // Power on the module
  dcf.enableModule();
  delay(1000);

  // Perform threshold calibration
  Serial.println("\n--- CALIBRATION PHASE (60 seconds) ---");
  dcf.calibrate();
  Serial.printf("Threshold set to: %d ms\n\n", dcf.getThreshold());

  // Start acquisition
  dcf.startAcquisition();
  syncStartTime = millis();

  // Choose execution mode
#if USE_TASK_MODE
  Serial.println("Starting background DCF77 task...");
  dcf.startTask(0, 10000, 1);  // Core 0, 10KB stack, priority 1
  Serial.println("Task mode active — monitoring in loop()\n");
#else
  Serial.println("Polling mode active — processSignal() in loop()\n");
#endif
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
#if USE_POLLING_MODE && !USE_TASK_MODE
  // Polling mode: call processSignal() frequently to feed the decoder
  dcf.processSignal();
#else
  // Task mode: background task handles signal processing
  // Just check state and print status
  delay(50);
#endif

  // ============================================================
  // STATUS REPORTING (every 5 seconds)
  // ============================================================

  if (millis() - statusPrintTime >= 5000) {
    statusPrintTime = millis();
    printStatus();
  }

  // ============================================================
  // CHECK FOR LOCK
  // ============================================================

  if (dcf.getLockState() == LockState::LOCKED) {
    onLocked();
  }

  // ============================================================
  // TIMEOUT (60s max acquisition)
  // ============================================================

  if (dcf.getSyncState() == SyncState::ACQUIRING &&
      millis() - syncStartTime > 120000) {
    Serial.println("\nAcquisition timeout — stopping.");
    dcf.stopAcquisition();
  }
}

// ============================================================
// STATUS PRINTER
// ============================================================

void printStatus() {
  Serial.printf("\n[%5lu ms] ", millis());

  // Lock state
  switch (dcf.getLockState()) {
    case LockState::UNLOCKED:
      Serial.print("UNLOCKED");
      break;
    case LockState::SYNCING:
      Serial.print("SYNCING ");
      break;
    case LockState::LOCKED:
      Serial.print("LOCKED  ");
      break;
  }
  Serial.print(" | ");

  // Sync state
  switch (dcf.getSyncState()) {
    case SyncState::IDLE:
      Serial.print("IDLE    ");
      break;
    case SyncState::ACQUIRING:
      Serial.print("ACQUIRE ");
      break;
    case SyncState::COMPLETE:
      Serial.print("DONE    ");
      break;
    case SyncState::FAILED:
      Serial.print("FAILED  ");
      break;
  }
  Serial.print(" | ");

  // Signal quality bar
  int quality = dcf.getSignalQuality();
  Serial.print("Signal: [");
  int filled = quality / 10;
  for (int i = 0; i < 10; i++) {
    Serial.print(i < filled ? "#" : "-");
  }
  Serial.printf("] %3d%% | ", quality);

  // Frame counts
  Serial.printf("Good:%d Bad:%d | ", dcf.getGoodFrameCount(), dcf.getBadFrameCount());

  // Bit count
  Serial.printf("Bits:%d\n", dcf.getBitCount());

  // Last error (if any)
  if (dcf.getLastError() != DecodeError::NO_ERROR) {
    Serial.printf("  Last error: 0x%02X\n", (uint8_t)dcf.getLastError());
  }
}

// ============================================================
// LOCK HANDLER — Called when LOCKED state achieved
// ============================================================

void onLocked() {
  static bool alreadyLogged = false;

  if (!alreadyLogged) {
    alreadyLogged = true;

    // Disable DCF77 module to save power
    dcf.disableModule();

    // Extract decoded time
    const DCF77Time& t = dcf.decodedTime();

    Serial.println("\n========================================");
    Serial.println("*** LOCK ACHIEVED ***");
    Serial.println("========================================");
    Serial.printf("Time: %02d:%02d\n", t.hour, t.minute);
    Serial.printf("Date: %02d/%02d/20%02d\n", t.day, t.month, t.year);
    Serial.printf("Day of week: %d (1=Mon, 7=Sun)\n", t.dow);
    Serial.printf("Timezone: %s\n", t.cest ? "CEST (UTC+2)" : "CET (UTC+1)");
    Serial.printf("Signal quality: %d%%\n", dcf.getSignalQuality());
    Serial.printf("Good frames: %d\n", dcf.getGoodFrameCount());
    Serial.println("========================================\n");

    // Stop acquisition and task
    dcf.stopAcquisition();
    if (dcf.isTaskRunning()) {
      dcf.stopTask();
    }

    // ========== DO SOMETHING WITH THE TIME ==========
    // For example, set system time, update a display, etc.

    Serial.println("You can now use dcf.decodedTime() in your application.");
    Serial.println("Press RESET to start a new acquisition.\n");

    // Prevent repeated logging
    delay(1000);
  }
}

