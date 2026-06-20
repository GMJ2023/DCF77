/**
 * @file DCF77.cpp
 * @brief DCF77 decoder implementation
 */

#include "DCF77.h"

// ============================================================
// BCD WEIGHT ARRAYS (for time field decoding)
// ============================================================
const int MIN_WEIGHTS[7]   = {1, 2, 4, 8, 10, 20, 40};
const int HOUR_WEIGHTS[6]  = {1, 2, 4, 8, 10, 20};
const int DAY_WEIGHTS[6]   = {1, 2, 4, 8, 10, 20};
const int MONTH_WEIGHTS[5] = {1, 2, 4, 8, 10};
const int YEAR_WEIGHTS[8]  = {1, 2, 4, 8, 10, 20, 40, 80};

// ============================================================
// CONSTRUCTOR & DESTRUCTOR
// ============================================================

DCF77::DCF77(int signalPin, int enablePin)
  : signalPin(signalPin),
    enablePin(enablePin),
    bitCount(0),
    validCount(0),
    noiseCount(0),
    noisyFrameCount(0),
    adaptiveThreshold(DCF77_BIT_THRESHOLD_DEFAULT),
    lastGoodThreshold(DCF77_BIT_THRESHOLD_DEFAULT),
    lockState(LockState::UNLOCKED),
    syncState(SyncState::IDLE),
    goodFrames(0),
    badFrames(0),
    goodDecodeHoldCount(0),
    lastMinute(-1),
    lastHour(-1),
    lastGoodDay(-1),
    lastGoodMonth(-1),
    lastGoodYear(-1),
    lastStableMinute(-1),
    driftCount(0),
    signalQuality(0),
    qualityIndex(0),
    taskHandle(nullptr),
    mutex(portMUX_INITIALIZER_UNLOCKED)
{
  memset(bits, 0, sizeof(bits));
  memset(qualityHistory, 0, sizeof(qualityHistory));
  memset(&decodedTimeData, 0, sizeof(decodedTimeData));
}

DCF77::~DCF77() {
  stopTask();
  disableModule();
}

// ============================================================
// INITIALIZATION
// ============================================================

bool DCF77::begin(bool pullup) {
  pinMode(signalPin, pullup ? INPUT_PULLUP : INPUT);
  if (enablePin >= 0) {
    pinMode(enablePin, OUTPUT);
    digitalWrite(enablePin, HIGH);  // Start disabled
  }
  resetFrame();
  return true;
}

void DCF77::enableModule() {
  if (enablePin >= 0) {
    digitalWrite(enablePin, LOW);
    delay(5000);  // Allow oscillator to settle
  }
}

void DCF77::disableModule() {
  if (enablePin >= 0) {
    digitalWrite(enablePin, HIGH);
  }
}

// ============================================================
// CALIBRATION
// ============================================================

bool DCF77::calibrate() {
  Serial.println("[DCF77] Calibrating threshold...");

  unsigned long calStart = millis();
  unsigned long calPulses[DCF77_CALIBRATION_SAMPLES];
  int calCount = 0;

  while (millis() - calStart < DCF77_CALIBRATION_TIME_MS) {
    // Progress indicator every 10s
    if ((millis() - calStart) % 10000 < 200) {
      Serial.printf("[DCF77] Calibrating... %lus, pulses: %d\n",
                    (millis() - calStart) / 1000, calCount);
    }

    // Wait for rising edge
    while (digitalRead(signalPin) == 0) {
      if (millis() - calStart >= DCF77_CALIBRATION_TIME_MS) break;
      vTaskDelay(1);
    }
    unsigned long riseTime = millis();

    // Measure high pulse
    while (digitalRead(signalPin) == 1) {
      vTaskDelay(1);
    }
    unsigned long pulseWidth = millis() - riseTime;

    // Wait for next rising edge to detect gap
    while (digitalRead(signalPin) == 0) {
      if (millis() - calStart >= DCF77_CALIBRATION_TIME_MS) break;
      vTaskDelay(1);
    }

    // Store if within plausible range
    if (pulseWidth >= DCF77_PULSE_MIN && pulseWidth <= DCF77_PULSE_MAX &&
        calCount < DCF77_CALIBRATION_SAMPLES) {
      calPulses[calCount++] = pulseWidth;
    }
  }

  // Not enough samples — use jump from last good
  if (calCount < DCF77_CALIBRATION_MIN_SAMPLES) {
    int jumpThreshold = lastGoodThreshold + DCF77_CALIBRATION_JUMP_MS;
    jumpThreshold = constrain(jumpThreshold, 80, 200);
    adaptiveThreshold = jumpThreshold;
    Serial.printf("[DCF77] Calibration insufficient (%d pulses) → jumping to %d ms\n",
                  calCount, adaptiveThreshold);
    return false;
  }

  // Sort pulses
  for (int i = 0; i < calCount - 1; i++) {
    for (int j = i + 1; j < calCount; j++) {
      if (calPulses[j] < calPulses[i]) {
        unsigned long tmp = calPulses[i];
        calPulses[i] = calPulses[j];
        calPulses[j] = tmp;
      }
    }
  }

  // Find largest gap (indicates cluster split)
  int splitIdx = 0;
  unsigned long maxGap = 0;
  for (int i = 0; i < calCount - 1; i++) {
    unsigned long gap = calPulses[i + 1] - calPulses[i];
    if (gap > maxGap && gap > 40) {
      maxGap = gap;
      splitIdx = i;
    }
  }

  // Check cluster balance
  int leftCount = splitIdx + 1;
  int rightCount = calCount - leftCount;
  if (leftCount < 5 || rightCount < 5) {
    int jumpThreshold = lastGoodThreshold + DCF77_CALIBRATION_JUMP_MS;
    jumpThreshold = constrain(jumpThreshold, 80, 200);
    adaptiveThreshold = jumpThreshold;
    Serial.printf("[DCF77] Unbalanced clusters (%d/%d) → jumping to %d ms\n",
                  leftCount, rightCount, adaptiveThreshold);
    return false;
  }

  // Calculate midpoint + upward bias
  int candidate = (calPulses[splitIdx] + calPulses[splitIdx + 1]) / 2 + 5;

  if (candidate >= 80 && candidate <= 180) {
    int jumpThreshold = candidate + DCF77_CALIBRATION_JUMP_MS;
    jumpThreshold = constrain(jumpThreshold, 80, 200);
    adaptiveThreshold = jumpThreshold;
    lastGoodThreshold = candidate;
    Serial.printf("[DCF77] Calibration: %d ms → jump to %d ms\n",
                  candidate, jumpThreshold);
    return true;
  } else {
    int jumpThreshold = lastGoodThreshold + DCF77_CALIBRATION_JUMP_MS;
    jumpThreshold = constrain(jumpThreshold, 80, 200);
    adaptiveThreshold = jumpThreshold;
    Serial.printf("[DCF77] Candidate out of range (%d ms) → jumping to %d ms\n",
                  candidate, jumpThreshold);
    return false;
  }
}

// ============================================================
// SYNC CONTROL
// ============================================================

void DCF77::startAcquisition() {
  portENTER_CRITICAL(&mutex);
  lockState = LockState::UNLOCKED;
  syncState = SyncState::ACQUIRING;
  bitCount = 0;
  noiseCount = 0;
  validCount = 0;
  goodFrames = 0;
  badFrames = 0;
  lastMinute = -1;
  lastHour = -1;
  lastGoodDay = -1;
  lastGoodMonth = -1;
  lastGoodYear = -1;
  lastStableMinute = -1;
  driftCount = 0;
  noisyFrameCount = 0;
  portEXIT_CRITICAL(&mutex);

  Serial.println("[DCF77] Acquisition started.");
}

void DCF77::stopAcquisition() {
  portENTER_CRITICAL(&mutex);
  syncState = SyncState::FAILED;
  portEXIT_CRITICAL(&mutex);

  Serial.println("[DCF77] Acquisition stopped.");
}

// ============================================================
// FRAME PROCESSING
// ============================================================

bool DCF77::processSignal() {
  // Wait for rising edge
  while (digitalRead(signalPin) == 0) {
    vTaskDelay(1);
  }
  unsigned long riseTime = millis();

  // Measure high pulse
  while (digitalRead(signalPin) == 1) {
    vTaskDelay(1);
  }
  unsigned long pulseWidth = millis() - riseTime;

  // Measure low gap
  while (digitalRead(signalPin) == 0) {
    vTaskDelay(1);
  }
  unsigned long gapWidth = millis() - riseTime - pulseWidth;

  // ============================================================
  // STAGE 1: HARD NOISE REJECTION
  // ============================================================

  // Kill obvious RF spikes
  if (pulseWidth < DCF77_PULSE_MIN || pulseWidth > DCF77_PULSE_MAX) {
    if (pulseWidth > 20 && pulseWidth < 800) {
      portENTER_CRITICAL(&mutex);
      noiseCount++;
      portEXIT_CRITICAL(&mutex);
    }
    return false;
  }

  // Reject invalid gaps
  if (gapWidth > 0 && gapWidth < DCF77_GAP_MIN_DEFAULT) {
    portENTER_CRITICAL(&mutex);
    noiseCount++;
    portEXIT_CRITICAL(&mutex);
    return false;
  }

  // ============================================================
  // MINUTE MARKER DETECTION
  // ============================================================

  if (gapWidth > DCF77_MINUTE_GAP) {
    processMinute();
    return true;  // Frame boundary detected
  }

  // ============================================================
  // STAGE 2: BIT CLASSIFICATION
  // ============================================================

  if (pulseWidth < 60 || pulseWidth > 300) {  // BIT_MIN/MAX
    portENTER_CRITICAL(&mutex);
    noiseCount++;
    portEXIT_CRITICAL(&mutex);
    return false;
  }

  bool bitValue = (pulseWidth > adaptiveThreshold);

  portENTER_CRITICAL(&mutex);
  if (bitCount < 60) {
    bits[bitCount++] = bitValue;
    validCount++;
  }
  portEXIT_CRITICAL(&mutex);

  return false;
}

// ============================================================
// DECODING
// ============================================================

int DCF77::readBCD(int start, int length, const int* weights) {
  int value = 0;
  for (int i = 0; i < length; i++) {
    if (bits[start + i]) value += weights[i];
  }
  return value;
}

DecodeError DCF77::validateFrame() {
  // Check frame structure
  if (bitCount < 58) return DecodeError::FRAME_SHORT;
  if (bits[0] != 0) return DecodeError::SANITY_CHECK;  // Bit 0 always 0
  if (bits[20] != 1) return DecodeError::SANITY_CHECK; // Bit 20 always 1

  // Check parity
  int pMin = 0;
  for (int i = 21; i <= 27; i++) pMin ^= bits[i];
  if (pMin != bits[28]) return DecodeError::PARITY_MIN;

  int pHour = 0;
  for (int i = 29; i <= 34; i++) pHour ^= bits[i];
  if (pHour != bits[35]) return DecodeError::PARITY_HOUR;

  int pDate = 0;
  for (int i = 36; i <= 57; i++) pDate ^= bits[i];
  if (pDate != bits[58]) return DecodeError::PARITY_DATE;

  // Decode fields
  int minute = readBCD(21, 7, MIN_WEIGHTS);
  int hour = readBCD(29, 6, HOUR_WEIGHTS);
  int day = readBCD(36, 6, DAY_WEIGHTS);
  int month = readBCD(45, 5, MONTH_WEIGHTS);
  int year = readBCD(50, 8, YEAR_WEIGHTS);

  // Range checks
  if (minute > 59 || hour > 23) return DecodeError::SANITY_CHECK;
  if (day < 1 || day > 31) return DecodeError::SANITY_CHECK;
  if (month < 1 || month > 12) return DecodeError::SANITY_CHECK;
  if (year < 24 || year > 35) return DecodeError::SANITY_CHECK;

  return DecodeError::NO_ERROR;
}

bool DCF77::tryDecode() {
  // Calculate signal quality
  int total = validCount + noiseCount;
  int quality = (total > 0) ? (validCount * 100) / total : 0;
  if (bitCount >= 58 && noiseCount == 0) quality = 100;
  updateQuality(quality);

  if (goodDecodeHoldCount > 0) goodDecodeHoldCount--;

  // Check for excessive noise
  if ((noiseCount > validCount * 2 && validCount > 0) ||
      (validCount == 0 && noiseCount == 0 && bitCount == 0)) {
    noisyFrameCount++;
    if (noisyFrameCount >= DCF77_MAX_NOISY_FRAMES) {
      Serial.println("[DCF77] Signal too noisy — aborting");
      syncState = SyncState::FAILED;
      return false;
    }
  } else {
    noisyFrameCount = 0;
  }

  // Validate frame
  lastError = validateFrame();

  if (lastError == DecodeError::NO_ERROR) {
    // Decode succeeded
    int minute = readBCD(21, 7, MIN_WEIGHTS);
    int hour = readBCD(29, 6, HOUR_WEIGHTS);
    int day = readBCD(36, 6, DAY_WEIGHTS);
    int month = readBCD(45, 5, MONTH_WEIGHTS);
    int year = readBCD(50, 8, YEAR_WEIGHTS);
    int dow = (bits[42] ? 1 : 0) | (bits[43] ? 2 : 0) | (bits[44] ? 4 : 0);
    bool cest = bits[17];

    // Check time progression
    bool timeValid = false;
    if (lastMinute == -1) {
      timeValid = true;
    } else {
      int expMinute = (lastMinute + 1) % 60;
      int expHour = lastHour + ((lastMinute + 1) / 60);
      if (expHour >= 24) expHour = 0;
      timeValid = (minute == expMinute && hour == expHour);
    }

    if (timeValid) {
      lastGoodThreshold = adaptiveThreshold;
      goodDecodeHoldCount = DCF77_GOOD_DECODE_HOLD_FRAMES;
      goodFrames++;

      // Check date coherence
      if (day == lastGoodDay && month == lastGoodMonth && year == lastGoodYear) {
        if (lockState == LockState::UNLOCKED && goodFrames >= 1) {
          lockState = LockState::SYNCING;
          Serial.println("[DCF77] SYNCING...");
        } else if (lockState == LockState::SYNCING &&
                   goodFrames >= DCF77_LOCK_THRESHOLD) {
          lockState = LockState::LOCKED;
          syncState = SyncState::COMPLETE;

          // Store decoded time
          decodedTimeData.minute = minute;
          decodedTimeData.hour = hour;
          decodedTimeData.day = day;
          decodedTimeData.month = month;
          decodedTimeData.year = year;
          decodedTimeData.dow = dow;
          decodedTimeData.cest = cest;

          Serial.printf("[DCF77] LOCKED! Time: %02d:%02d %02d/%02d/20%02d\n",
                       hour, minute, day, month, year);
          return true;
        }
      }

      lastGoodDay = day;
      lastGoodMonth = month;
      lastGoodYear = year;
      lastMinute = minute;
      lastHour = hour;
    } else {
      badFrames++;
      goodFrames = 0;
      lastMinute = -1;
      lastHour = -1;
    }
  } else {
    // Decode failed — intelligent self-heal
    if (lockState == LockState::UNLOCKED && goodDecodeHoldCount == 0) {
      // Try to nudge threshold based on which parity failed
      if ((lastError & DecodeError::PARITY_MIN) &&
          !(lastError & DecodeError::PARITY_HOUR)) {
        adaptiveThreshold += 2;
        adaptiveThreshold = constrain(adaptiveThreshold, 80, 200);
        Serial.printf("[DCF77] Self-heal: nudged UP to %d ms\n", adaptiveThreshold);
      } else if (lastError & DecodeError::PARITY_HOUR) {
        adaptiveThreshold -= 2;
        adaptiveThreshold = constrain(adaptiveThreshold, 80, 200);
        Serial.printf("[DCF77] Self-heal: nudged DOWN to %d ms\n", adaptiveThreshold);
      }
    }

    if (lockState != LockState::SYNCING) {
      badFrames++;
      goodFrames = 0;
    }
    lastMinute = -1;
    lastHour = -1;
  }

  if (badFrames >= DCF77_UNLOCK_THRESHOLD) {
    lockState = LockState::UNLOCKED;
    lastMinute = -1;
    lastHour = -1;
    Serial.println("[DCF77] UNLOCKED");
  }

  resetFrame();
  return false;
}

// ============================================================
// PRIVATE HELPERS
// ============================================================

void DCF77::processMinute() {
  portENTER_CRITICAL(&mutex);
  int bc = bitCount;
  int vc = validCount;
  int nc = noiseCount;
  portEXIT_CRITICAL(&mutex);

  Serial.printf("[DCF77] Minute marker — bits:%d valid:%d noise:%d\n", bc, vc, nc);

  // Dispatch decode attempt (will call resetFrame internally)
  tryDecode();
}

void DCF77::resetFrame() {
  portENTER_CRITICAL(&mutex);
  bitCount = 0;
  noiseCount = 0;
  validCount = 0;
  portEXIT_CRITICAL(&mutex);
}

void DCF77::updateQuality(int quality) {
  qualityHistory[qualityIndex] = quality;
  qualityIndex = (qualityIndex + 1) % 5;
  int total = 0;
  for (int i = 0; i < 5; i++) total += qualityHistory[i];
  signalQuality = total / 5;
}

// ============================================================
// ASYNC TASK MODE
// ============================================================

bool DCF77::startTask(int coreID, uint32_t stackSize, uint32_t priority) {
  if (taskHandle != nullptr) return false;  // Already running

  BaseType_t result = xTaskCreatePinnedToCore(
    taskWrapper,           // Task function
    "DCF77",               // Task name
    stackSize,             // Stack size
    (void*)this,           // Parameter (pass this pointer)
    priority,              // Priority
    &taskHandle,           // Task handle
    coreID                 // Core affinity
  );

  if (result == pdPASS) {
    Serial.printf("[DCF77] Task started on core %d\n", coreID);
    return true;
  }
  return false;
}

void DCF77::stopTask() {
  if (taskHandle != nullptr) {
    vTaskDelete(taskHandle);
    taskHandle = nullptr;
    Serial.println("[DCF77] Task stopped.");
  }
}

void DCF77::taskWrapper(void* parameter) {
  DCF77* pThis = (DCF77*)parameter;
  pThis->taskLoop();
}

void DCF77::taskLoop() {
  startAcquisition();

  for (;;) {
    if (syncState == SyncState::ACQUIRING) {
      processSignal();
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

