/**
 * @file DCF77.h
 * @brief DCF77 Radio Clock Signal Decoder Library
 * 
 * A flexible, production-tested library for decoding the DCF77 atomic clock
 * signal. Provides real-time frame detection, adaptive threshold calibration,
 * and robust error handling on ESP32 (or compatible) microcontrollers.
 * 
 * **Architecture:**
 * - Runs on dedicated task (FreeRTOS) or as polling function
 * - Dual-core safe with mutex protection
 * - Non-blocking signal processing
 * - Intelligent self-healing threshold calibration
 * - Full parity validation and BCD decoding
 * 
 * **Hardware Requirements:**
 * - DCF77 module with digital pulse output
 * - GPIO pin for signal input
 * - GPIO pin for module enable (optional)
 * - ~10KB RAM for frame buffer and state
 * 
 * @author Digconn Systems Limited
 * @date 2024–2025
 * @license MIT
 * 
 * **Example:**
 * ```cpp
 * DCF77 dcf(GPIO_DCF77_PIN, GPIO_ENABLE_PIN);
 * dcf.enableModule();
 * 
 * // Trigger calibration and decoding on demand
 * if (dcf.decode()) {
 *   int hour = dcf.getHour();
 *   int minute = dcf.getMinute();
 *   // Use decoded time...
 * }
 * ```
 */

#ifndef DCF77_H
#define DCF77_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================
// CONFIGURATION CONSTANTS
// ============================================================

/** @defgroup DCF77_Thresholds Signal Detection Thresholds
 *  @{
 */
#define DCF77_BIT_THRESHOLD_DEFAULT    145    ///< Default midpoint for 0/1 classification (ms)
#define DCF77_PULSE_MIN                60     ///< Minimum valid pulse width (ms)
#define DCF77_PULSE_MAX                350    ///< Maximum valid pulse width (ms)
#define DCF77_GAP_MIN_DEFAULT          550    ///< Minimum gap width for valid bit (ms)
#define DCF77_MINUTE_GAP               1450   ///< Minute marker gap threshold (ms)
/** @} */

/** @defgroup DCF77_Calibration Calibration Parameters
 *  @{
 */
#define DCF77_CALIBRATION_TIME_MS      60000  ///< Duration for threshold calibration (60s)
#define DCF77_CALIBRATION_SAMPLES      40     ///< Max pulses to collect during calibration
#define DCF77_CALIBRATION_MIN_SAMPLES  10     ///< Minimum samples for valid calibration
#define DCF77_CALIBRATION_JUMP_MS      20     ///< Upward bias when threshold found (ms)
#define DCF77_MAX_NOISY_FRAMES         6      ///< Max consecutive noisy frames before abort
/** @} */

/** @defgroup DCF77_Sync Sync Logic Parameters
 *  @{
 */
#define DCF77_LOCK_THRESHOLD           2      ///< Frames to achieve SYNCING state
#define DCF77_UNLOCK_THRESHOLD         3      ///< Consecutive errors to drop LOCKED
#define DCF77_GOOD_DECODE_HOLD_FRAMES  6      ///< Hold good threshold for N frames after decode
/** @} */

// ============================================================
// ENUMERATIONS
// ============================================================

/**
 * @enum LockState
 * @brief DCF77 synchronisation state machine
 */
enum class LockState : uint8_t {
  UNLOCKED = 0,  ///< No valid frames detected
  SYNCING = 1,   ///< Valid frames accumulating toward lock
  LOCKED = 2     ///< Stable lock on time signal
};

/**
 * @enum SyncState
 * @brief Frame acquisition progress
 */
enum class SyncState : uint8_t {
  IDLE = 0,       ///< Not acquiring
  ACQUIRING = 1,  ///< Listening for frames
  COMPLETE = 2,   ///< Lock achieved
  FAILED = 3      ///< Timeout or abort
};

/**
 * @enum DecodeError
 * @brief Parity check failure details
 */
enum class DecodeError : uint8_t {
  NO_ERROR = 0,
  PARITY_MIN = 1,    ///< Minutes parity failed
  PARITY_HOUR = 2,   ///< Hours parity failed
  PARITY_DATE = 4,   ///< Date parity failed
  SANITY_CHECK = 8,  ///< Value range check failed (e.g., hour > 23)
  FRAME_SHORT = 16   ///< Frame < 58 bits
};

// ============================================================
// TIME STRUCTURE
// ============================================================

/**
 * @struct DCF77Time
 * @brief Decoded time fields from DCF77 frame
 */
struct DCF77Time {
  uint8_t minute;  ///< 0–59
  uint8_t hour;    ///< 0–23 (UTC)
  uint8_t day;     ///< 1–31
  uint8_t month;   ///< 1–12
  uint8_t year;    ///< 24–99 (20xx)
  uint8_t dow;     ///< 0–6 (Monday=1, Sunday=7; DCF77 uses 1–7)
  bool cest;       ///< Central European Summer Time flag
};

// ============================================================
// MAIN CLASS
// ============================================================

/**
 * @class DCF77
 * @brief High-level DCF77 decoder with adaptive calibration
 * 
 * Provides frame detection, bit classification, time decoding, and
 * automatic threshold calibration. Designed for dual-core ESP32 but
 * works with any microcontroller with GPIO and microsecond timers.
 */
class DCF77 {
public:
  /**
   * @brief Constructor with pin configuration
   * @param signalPin GPIO connected to DCF77 module output
   * @param enablePin GPIO controlling module power (set to -1 if always on)
   */
  DCF77(int signalPin, int enablePin = -1);

  /**
   * @brief Destructor; cleans up task if running
   */
  ~DCF77();

  // ========== INITIALIZATION ==========

  /**
   * @brief Initialize GPIO and internal state
   * @param pullup If true, configure signal pin with internal pullup
   * @return true if successful
   */
  bool begin(bool pullup = true);

  /**
   * @brief Power on the DCF77 module
   * @details Applies enable signal and waits 5s for oscillator settle
   */
  void enableModule();

  /**
   * @brief Power off the DCF77 module
   * @details Releases enable signal to save power
   */
  void disableModule();

  // ========== CALIBRATION ==========

  /**
   * @brief Perform adaptive threshold calibration
   * @details Listens for 60s, measures pulse widths, finds the largest gap
   * between pulse clusters, and sets threshold midpoint.
   * 
   * Should be called once per sync session before frame acquisition.
   * 
   * @return true if calibration succeeded with minimum 10 pulses
   */
  bool calibrate();

  /**
   * @brief Get current adaptive threshold
   * @return Threshold in milliseconds
   */
  int getThreshold() const { return adaptiveThreshold; }

  /**
   * @brief Manually set threshold (for testing or persistence)
   * @param threshold Value in milliseconds (clamped to 80–200)
   */
  void setThreshold(int threshold) {
    adaptiveThreshold = constrain(threshold, 80, 200);
  }

  // ========== SYNC CONTROL ==========

  /**
   * @brief Start listening for DCF77 frames
   * @details Resets decoder state and begins frame assembly.
   * Non-blocking; call decode() periodically or use task mode.
   */
  void startAcquisition();

  /**
   * @brief Stop frame acquisition
   * @details Useful for aborting if timeout or manual trigger
   */
  void stopAcquisition();

  /**
   * @brief Process one bit/frame; call frequently from loop() or dedicated task
   * @details This is the low-level interrupt equivalent. Reads GPIO directly.
   * Called by the task if using async mode.
   * 
   * @return true if a complete frame was just assembled (call decodedTime() to get it)
   */
  bool processSignal();

  /**
   * @brief Attempt to decode current frame buffer
   * @details Checks parity, sanity ranges, and state progression.
   * Called automatically on minute boundaries.
   * 
   * @return true if a valid, coherent time was decoded
   */
  bool tryDecode();

  /**
   * @brief Check if module has achieved lock on time signal
   * @return Current LockState
   */
  LockState getLockState() const { return lockState; }

  /**
   * @brief Check acquisition progress
   * @return Current SyncState
   */
  SyncState getSyncState() const { return syncState; }

  // ========== TIME ACCESS ==========

  /**
   * @brief Get last successfully decoded time
   * @return const DCF77Time with minute, hour, day, etc.
   */
  const DCF77Time& decodedTime() const { return decodedTimeData; }

  int getMinute() const { return decodedTimeData.minute; }
  int getHour() const { return decodedTimeData.hour; }
  int getDay() const { return decodedTimeData.day; }
  int getMonth() const { return decodedTimeData.month; }
  int getYear() const { return decodedTimeData.year; }
  int getDow() const { return decodedTimeData.dow; }
  bool isCEST() const { return decodedTimeData.cest; }

  // ========== DIAGNOSTICS ==========

  /**
   * @brief Get signal quality as percentage
   * @return 0–100% (valid bits / total bits sampled)
   */
  int getSignalQuality() const { return signalQuality; }

  /**
   * @brief Get count of consecutive good frames
   * @return Number of frames in agreement on time progression
   */
  int getGoodFrameCount() const { return goodFrames; }

  /**
   * @brief Get count of failed/corrupted frames
   * @return Number of frames dropped due to parity/sanity errors
   */
  int getBadFrameCount() const { return badFrames; }

  /**
   * @brief Get last decode error (if any)
   * @return DecodeError enum flags
   */
  DecodeError getLastError() const { return lastError; }

  /**
   * @brief Get raw bit array (for debugging)
   * @return Pointer to 60-element bits[] array
   */
  const uint8_t* getRawBits() const { return bits; }

  /**
   * @brief Get count of bits collected in current frame
   * @return 0–60
   */
  int getBitCount() const { return bitCount; }

  // ========== ASYNC TASK MODE (Optional) ==========

  /**
   * @brief Create and start a FreeRTOS task for continuous decoding
   * @param coreID Core affinity (0 or 1 on ESP32; default 0)
   * @param stackSize Stack depth in words (default 10000)
   * @param priority FreeRTOS priority (default 1)
   * @return true if task created successfully
   * 
   * **Note:** When using task mode, do NOT call processSignal() from loop();
   * only call tryDecode() and state getters.
   */
  bool startTask(int coreID = 0, uint32_t stackSize = 10000, uint32_t priority = 1);

  /**
   * @brief Stop and destroy the background task
   */
  void stopTask();

  /**
   * @brief Check if background task is running
   * @return true if task is active
   */
  bool isTaskRunning() const { return taskHandle != nullptr; }

private:
  // ========== GPIO PINS ==========
  int signalPin;
  int enablePin;

  // ========== SIGNAL PROCESSING STATE ==========
  uint8_t bits[60];           ///< Raw bit array (0 or 1 per DCF77 bit)
  int bitCount;               ///< Current position in frame (0–60)
  int validCount;             ///< Bits classified as valid (not noise)
  int noiseCount;             ///< Bits marked as noise
  int noisyFrameCount;        ///< Consecutive noisy frame counter

  // ========== ADAPTIVE THRESHOLD ==========
  int adaptiveThreshold;      ///< Current bit discrimination threshold (ms)
  int lastGoodThreshold;      ///< Last known good threshold (for fallback)

  // ========== LOCK & SYNC STATE MACHINE ==========
  LockState lockState;        ///< UNLOCKED → SYNCING → LOCKED
  SyncState syncState;        ///< IDLE, ACQUIRING, COMPLETE, FAILED
  int goodFrames;             ///< Count of consecutive coherent frames
  int badFrames;              ///< Count of errors since last good frame
  int goodDecodeHoldCount;    ///< Frame holdover counter after decode

  // ========== TIME STATE TRACKING ==========
  int lastMinute;             ///< Last valid minute value
  int lastHour;               ///< Last valid hour value
  int lastGoodDay;            ///< Last valid day (for coherence check)
  int lastGoodMonth;
  int lastGoodYear;
  int lastStableMinute;       ///< Minute value for drift detection
  int driftCount;             ///< Consecutive frame drift counter

  // ========== DECODED TIME ==========
  DCF77Time decodedTimeData;  ///< Last successfully decoded time
  DecodeError lastError;      ///< Error code from last decode attempt

  // ========== SIGNAL QUALITY ==========
  int signalQuality;          ///< Percentage of valid vs total bits
  int qualityHistory[5];      ///< Rolling 5-frame average
  int qualityIndex;

  // ========== ASYNC TASK ==========
  TaskHandle_t taskHandle;    ///< FreeRTOS task handle (nullptr if not running)
  portMUX_TYPE mutex;         ///< Mutex for shared state access

  // ========== PRIVATE METHODS ==========

  /**
   * @brief FreeRTOS task entry point (static wrapper)
   * @param parameter Pointer to DCF77 instance (void*)
   */
  static void taskWrapper(void* parameter);

  /**
   * @brief Real task loop (runs on dedicated core)
   */
  void taskLoop();

  /**
   * @brief Process one complete DCF77 minute (60-bit frame)
   * @details Called on minute marker (gap > MINUTE_GAP)
   */
  void processMinute();

  /**
   * @brief Decode BCD-encoded field from frame
   * @param start Bit position (21–59)
   * @param length Number of bits
   * @param weights BCD weight array (powers of 2)
   * @return Decoded integer value
   */
  int readBCD(int start, int length, const int* weights);

  /**
   * @brief Check parity and range validity on decoded frame
   * @return DecodeError flags (NO_ERROR if all checks pass)
   */
  DecodeError validateFrame();

  /**
   * @brief Reset frame buffer and counters for next frame
   */
  void resetFrame();

  /**
   * @brief Update rolling signal quality average
   * @param quality Latest sample (0–100)
   */
  void updateQuality(int quality);
};

#endif // DCF77_H
