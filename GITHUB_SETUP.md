# DCF77 Library Directory Structure

This guide explains how to organize and deploy the DCF77 library on GitHub.

---

## Recommended GitHub Repository Structure

```
DCF77/
│
├── README.md                       # Main documentation
├── LICENSE                         # MIT License
├── library.properties              # Arduino IDE metadata
├── CONTRIBUTING.md                 # Contribution guidelines
│
├── src/
│   ├── DCF77.h                    # Main header (public API)
│   └── DCF77.cpp                  # Implementation
│
├── examples/
│   ├── DCF77_Example/             # Simple polling example
│   │   └── DCF77_Example.ino
│   │
│   ├── DCF77_Advanced_Example/    # Advanced with diagnostics
│   │   └── DCF77_Advanced_Example.ino
│   │
│   └── DCF77_TaskMode/            # Background task example
│       └── DCF77_TaskMode.ino
│
├── docs/
│   ├── API_REFERENCE.md           # Detailed API docs
│   ├── HARDWARE_SETUP.md          # Wiring diagrams
│   ├── TROUBLESHOOTING.md         # Common issues
│   └── DCF77_SIGNAL_FORMAT.md     # Technical reference
│
├── .github/
│   └── ISSUE_TEMPLATE.md          # Bug report template
│
└── .gitignore                      # Standard Arduino/C++ ignores

```

---

## Installation Instructions for Users

### Arduino IDE (Recommended)

1. Download latest **Release** from GitHub (as ZIP)
2. Extract to: `Documents/Arduino/libraries/DCF77/`
3. Restart Arduino IDE
4. File → Examples → DCF77 → DCF77_Example

### Arduino Library Manager

(After initial release)

1. Sketch → Include Library → Manage Libraries...
2. Search: "DCF77"
3. Click Install

### Manual (PlatformIO)

Add to `platformio.ini`:
```ini
lib_deps =
    https://github.com/digconn-systems/DCF77.git
```

---

## Quick File Mapping

| File | What It Is | What Users See |
|------|-----------|-----------------|
| `src/DCF77.h` | Public API header | Included via `#include "DCF77.h"` |
| `src/DCF77.cpp` | Implementation | Auto-linked during compilation |
| `examples/*/` | Sample sketches | File → Examples menu in Arduino IDE |
| `README.md` | Overview | GitHub front page + NPM/docs sites |
| `library.properties` | Metadata | Arduino IDE library listing |
| `LICENSE` | Legal | GitHub "License" tab |

---

## Before Pushing to GitHub

Checklist:

- [ ] All `.ino` files compile without warnings
- [ ] Tested on **ESP32-S3 Dev Module** (or document board versions)
- [ ] All examples have clear comments
- [ ] `library.properties` version matches source
- [ ] `README.md` is complete and typo-free
- [ ] All API docs in header files use Doxygen format
- [ ] License year is current
- [ ] `.gitignore` ignores build artifacts (`.o`, `*.a`, etc.)

---

## GitHub Release Checklist

For each release tag (e.g., `v1.0.0`):

1. Update version in:
   - `library.properties`: `version=1.0.0`
   - Header comment in `DCF77.h`
   - `README.md` (version badge)

2. Create **Release Notes** with:
   - Summary of changes
   - Known issues
   - Hardware compatibility
   - Installation command

3. Tag commit: `git tag v1.0.0 && git push --tags`

4. Publish release on GitHub with ZIP download

---

## File Placement Details

### Arduino IDE Scans

When user installs via IDE or Library Manager, it looks for:

```
DCF77/
├── library.properties          ← REQUIRED (metadata)
├── README.md                   ← Shown on IDE "More info" 
├── src/                        ← IDE auto-includes *.h/*.cpp
│   ├── *.h
│   └── *.cpp
└── examples/                   ← Shown in File → Examples
    ├── ExampleName1/
    │   └── ExampleName1.ino
    └── ExampleName2/
        └── ExampleName2.ino
```

### Key Rules

- `.ino` files **must** be in `examples/FolderName/FolderName.ino` (exact match)
- `library.properties` must be in **root** of repo
- `src/` is optional but recommended for clean structure
- All header includes should use **relative paths** (e.g., `#include "DCF77.h"`, not `<DCF77.h>` for local testing)

---

## .gitignore Template

```
# Build artifacts
*.o
*.a
*.so
*.elf
*.exe
build/
dist/

# IDE
.vscode/
.idea/
*.swp
*.swo
*~

# OS
.DS_Store
Thumbs.db

# Python (if doing tests)
__pycache__/
*.pyc
.pytest_cache/

# Doxygen (if using)
docs/html/
docs/latex/

```

---

## Directory Structure — Detailed Explanation

### `src/`

**Why separate from examples?**
- Arduino IDE & PlatformIO expect source in `src/`
- Keeps compiled `.o` files out of examples
- Professional library structure

**Contents:**
- `DCF77.h` — Public class definition + API documentation
- `DCF77.cpp` — Implementation (signal processing, decoding)

### `examples/`

**Arduino IDE Requirement:** Each example must be in its own folder matching the `.ino` filename.

```
examples/
├── DCF77_Example/           ← Folder named after .ino file
│   └── DCF77_Example.ino    ← EXACT match required
│
├── DCF77_Advanced_Example/
│   └── DCF77_Advanced_Example.ino
```

**Why?** Arduino IDE uses folder name to group in menu.

### `docs/`

Optional but recommended for comprehensive documentation:

- `API_REFERENCE.md` — Full function reference (also in header)
- `HARDWARE_SETUP.md` — Wiring diagrams, module links
- `TROUBLESHOOTING.md` — Common issues & solutions
- `DCF77_SIGNAL_FORMAT.md` — Bit layout, technical spec

---

## How to Test Locally

### Test Arduino IDE Integration

1. Copy entire folder to `~/Documents/Arduino/libraries/DCF77/`
2. Restart Arduino IDE
3. Open File → Examples → DCF77
4. Verify examples appear and compile

### Test PlatformIO

Create a test project:

```bash
mkdir test_dcf77
cd test_dcf77
pio init --board esp32-s3-devkitc-1
echo 'lib_deps = file://../DCF77' >> platformio.ini
pio run -e esp32-s3-devkitc-1
```

---

## Continuous Integration (Optional)

Consider GitHub Actions to auto-check:

```yaml
# .github/workflows/compile.yml
name: Compile Examples

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - uses: arduino/setup-arduino-cli@v1
      - run: arduino-cli core install esp32:esp32
      - run: |
          for file in examples/*/*.ino; do
            arduino-cli compile --fqbn esp32:esp32:esp32s3 "$file"
          done
```

---

## Package Management

### npm / PlatformIO Registry

Once published:

**PlatformIO:**
```ini
lib_deps =
    digconn-systems/DCF77 @ ^1.0.0
```

**Arduino Library Index:**
- Indexed automatically after first GitHub release
- Search "DCF77" in Library Manager

---

## Branching Strategy (Recommended)

```
main          ← Stable releases only (tagged)
 └─ develop   ← Integration branch (pre-release testing)
     └─ feature/* ← Individual features/fixes
```

On each merge to `main`, create a **Release** tag.

---

## Summary for GitHub Upload

**Essential files:**
1. `src/DCF77.h` & `src/DCF77.cpp`
2. `examples/DCF77_Example/DCF77_Example.ino`
3. `README.md`
4. `LICENSE`
5. `library.properties`

**Nice to have:**
- `docs/`
- `.github/ISSUE_TEMPLATE.md`
- `.gitignore`
- Multiple examples

**Deploy to GitHub:**
```bash
git init
git add .
git commit -m "Initial commit: DCF77 library v1.0.0"
git remote add origin https://github.com/username/DCF77.git
git push -u origin main
git tag v1.0.0
git push --tags
```

---

That's it! Your library is now ready for the Arduino ecosystem. 🚀

