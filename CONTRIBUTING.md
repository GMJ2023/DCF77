# Contributing to DCF77

Thanks for helping improve the DCF77 library! This document outlines how to contribute.

---

## Code of Conduct

- Be respectful and inclusive
- Provide constructive feedback
- No harassment, discrimination, or abuse
- Treat all contributors fairly

---

## How to Report Bugs

**Before submitting:**
- Check existing [Issues](../../issues) — your bug may be known
- Verify with the latest `develop` branch
- Test with a simple example (not your full project)

**When reporting:**

Use the Bug template (auto-filled) with:

1. **Title:** Clear, descriptive (e.g., "DCF77 locks intermittently on ESP32-S3")

2. **Environment:**
   - Board: `ESP32-S3 Dev Module` (or exact model)
   - Arduino IDE version: `2.0.3`
   - Library version: `1.0.0` (or commit hash)
   - DCF77 module: `Conrad ELV-style` (or model number)

3. **Expected behavior:** "Lock should achieve in 2–5 minutes with good signal"

4. **Actual behavior:** "Locks intermittently, sometimes never"

5. **Steps to reproduce:**
   ```
   1. Compile DCF77_Example.ino
   2. Upload to ESP32-S3
   3. Open serial monitor
   4. Wait 5 minutes
   5. Watch threshold oscillate without locking
   ```

6. **Debug output:** Paste relevant serial log (50–100 lines)

7. **Screenshots/Logs:** If applicable, attach wave captures or signal quality graphs

---

## Feature Requests

**Before proposing:**
- Check closed issues — may have been declined before
- Consider if feature fits library scope (DCF77 decode only, not time display)

**When proposing:**

Use the Feature template with:

1. **Title:** "Add STM32 support" or "Implement frame pre-filter"

2. **Problem:** Why is this needed?
   - "Many users on STM32H743 boards but library is ESP32-only"

3. **Proposed solution:** How would it work?
   - "Decouple FreeRTOS dependency; use generic GPIO polling"

4. **Alternatives considered:** What else?
   - "Option A: Wrap FreeRTOS in #ifdef (ugly)"
   - "Option B: Full arch abstraction layer (complex)"

---

## Pull Request Process

### 1. Fork & Branch

```bash
git clone https://github.com/YOUR_USERNAME/DCF77.git
cd DCF77
git checkout develop
git pull origin develop
git checkout -b feature/your-feature-name
```

### 2. Code Style

**Formatting:**
- Use 2-space indentation (not tabs)
- Max line length: 100 characters
- Brace style: Allman (opening brace on new line for functions)

```cpp
// ✓ GOOD
void myFunction()
{
  if (condition) {
    doSomething();
  }
}

// ✗ BAD
void myFunction() {
  if (condition) doSomething();
}
```

**Naming:**
- Functions: `camelCase` (e.g., `processSignal()`)
- Classes: `PascalCase` (e.g., `DCF77`, `LockState`)
- Constants: `UPPER_SNAKE_CASE` (e.g., `DCF77_PULSE_MIN`)
- Private members: `camelCase` with leading `_` or comment `// private`

**Comments:**
- Use `//` for single-line comments
- Use `/** ... */` for Doxygen documentation
- Explain *why*, not *what* (code explains what)

```cpp
// ✓ GOOD
// Nudge threshold down if hour parity failed (was too high)
adaptiveThreshold -= 2;

// ✗ BAD
// Subtract 2 from threshold
adaptiveThreshold -= 2;
```

### 3. Write Tests

For any change to decoding logic:
- Test on real hardware (ESP32-S3 preferred)
- Include "before" and "after" signal quality
- Note threshold values used
- Document any edge cases discovered

Example test report:
```
Tested fix for drift detection on GDEY0579T93:
- Before: Lost lock after 2–3 frames on poor signal
- After: Stable lock maintained, 6+ frames agreement
- Tested with: 3 different rooms, multiple orientations
- Signal quality improved from 62% to 89% average
```

### 4. Update Documentation

If changing API or behavior:
- Update `DCF77.h` Doxygen comments
- Update `README.md` API Reference section
- Add example code if non-obvious
- Update `CHANGELOG.md` (if present)

### 5. Compile & Test

```bash
# Test compile for ESP32-S3
pio run -e esp32-s3-devkitc-1

# Run all examples
for file in examples/*/*.ino; do
  pio run -e esp32-s3-devkitc-1 --project-option="src_dir=$file"
done
```

### 6. Commit & Push

Write clear commit messages:

```bash
# ✓ GOOD
git commit -m "Fix threshold drift in poor signal conditions

- Add MAX_NOISY_FRAMES counter to abort after 6 consecutive errors
- Self-heal logic now waits for good frame before adjusting
- Improves lock stability on noisy urban sites
- Fixes issue #42"

# ✗ BAD
git commit -m "fix stuff"
```

Push to your fork:
```bash
git push origin feature/your-feature-name
```

### 7. Submit Pull Request

1. Go to [Pull Requests](../../pulls) on GitHub
2. Click "New Pull Request"
3. Base: `develop` ← Compare: `your-fork:feature/your-feature`
4. Fill in template:

```markdown
## Description
Fixes issue #42 (brief description of what's fixed)

## Changes
- Added MAX_NOISY_FRAMES counter
- Self-heal logic waits for stable frame
- Added example of poor-signal recovery

## Testing
- Tested on: ESP32-S3 Dev Module, Conrad DCF77 module
- Environment: Urban setting, poor signal (35% quality)
- Before: Lock lost after 2–3 minutes
- After: Stable lock maintained for 30+ minutes
- No regressions on good-signal tests (indoor, high quality)

## Checklist
- [x] Code compiles without warnings
- [x] Examples still build
- [x] Doxygen comments added/updated
- [x] README updated (if API change)
- [x] Tested on hardware
```

### 8. Respond to Review

Maintainer will review for:
- ✓ Code style consistency
- ✓ API stability (no breaking changes in minor versions)
- ✓ Performance impact (< 1% CPU for polling mode)
- ✓ Documentation completeness
- ✓ Hardware compatibility

If changes requested:
```bash
# Make changes
git add .
git commit -m "Address review feedback: clarify threshold logic"
git push origin feature/your-feature-name
# GitHub auto-updates the PR
```

---

## Architecture Notes (For Contributors)

### Signal Processing Pipeline

```
GPIO pulse → Hard noise reject → Minute marker detect
   ↓                               ↓
Bit classification ← Adaptive threshold ← Calibration
   ↓
Frame assembly (60 bits)
   ↓
BCD decode + parity check
   ↓
State machine (UNLOCKED → SYNCING → LOCKED)
   ↓
Time available via decodedTime()
```

### Key State Variables

- `bits[60]`: Raw DCF77 bits
- `bitCount`: Position in frame (0–60)
- `adaptiveThreshold`: Current discrimination threshold (ms)
- `lockState`: UNLOCKED / SYNCING / LOCKED
- `syncState`: IDLE / ACQUIRING / COMPLETE / FAILED

### Thread Safety

- Mutex `portMUX_TYPE mux` guards shared state
- `portENTER_CRITICAL(&mux)` before read/write
- `portEXIT_CRITICAL(&mux)` after

Example:
```cpp
portENTER_CRITICAL(&mux);
bits[bitCount++] = bitValue;
validCount++;
portEXIT_CRITICAL(&mux);
```

---

## Areas Looking for Contribution

- [ ] **STM32 / SAMD51 port** — Remove FreeRTOS dependency
- [ ] **SPIFFS persistence** — Save threshold for faster restart
- [ ] **Advanced frame recovery** — Better drift detection
- [ ] **BLE timezone sync** — Auto-set UTC offset
- [ ] **Performance profiling** — Reduce RAM/CPU on weak boards
- [ ] **Documentation translations** — German, French, Spanish
- [ ] **CI/CD improvements** — GitHub Actions for auto-compile

---

## Release Cycle

Releases follow [Semantic Versioning](https://semver.org/):

- **1.0.0** → Major (breaking API changes)
- **1.1.0** → Minor (new features, backward compatible)
- **1.0.1** → Patch (bug fixes only)

Maintainer merges `develop` → `main` every **3 months** or when 5+ features complete.

---

## Getting Help

- **General questions?** Open an Issue with label `question`
- **Design discussion?** Start a GitHub Discussion
- **Want to chat?** Contact maintainer at `geoff@digconn.co.uk`

---

## Recognition

Contributors are recognized in:
- Release notes (by feature)
- `CONTRIBUTORS.md` file (comprehensive list)
- Project README (top contributors)

---

Thanks for contributing! 🎉

