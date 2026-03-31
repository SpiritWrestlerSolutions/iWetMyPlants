# Contributing to iWetMyPlants

First off — thank you. This is a solo project and every contribution, whether it's a detailed bug report, a documentation fix, or a full pull request, genuinely makes a difference.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [How Can I Contribute?](#how-can-i-contribute)
 - [Reporting Bugs](#reporting-bugs)
 - [Requesting Features](#requesting-features)
 - [Improving Documentation](#improving-documentation)
 - [Contributing Code](#contributing-code)
- [Development Setup](#development-setup)
- [Pull Request Process](#pull-request-process)
- [Style Guidelines](#style-guidelines)

---

## Code of Conduct

This project follows a simple rule: be kind. See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) for the full version.

---

## How Can I Contribute?

### Reporting Bugs

Bug reports are enormously valuable — especially clear ones. Before opening an issue, please check the [Troubleshooting Guide](https://spiritwrestler.ca/plants/documentation/troubleshooting.html) to see if your issue is already documented.

When opening a bug report, please include:

- **Hardware:** Which device (Hub / Remote / Greenhouse), which ESP32 board, which sensors
- **Firmware version:** Shown on the installer page or in the Hub's web UI
- **What you did:** Step-by-step if possible
- **What you expected to happen**
- **What actually happened:** Serial output is gold here if you have it
- **Browser and OS** if it's a web installer or documentation issue

Use the [Bug Report template](.github/ISSUE_TEMPLATE/bug_report.md) when opening an issue, or use the feedback button on the installer page — it goes straight to my inbox.

### Requesting Features

Feature requests are welcome. Please open an issue with the `enhancement` label and describe:

- **What problem does this solve?** (Use cases help enormously)
- **What would the behaviour look like?** (Be as specific as you can)
- **Are you willing to help implement it?** (No pressure either way)

### Improving Documentation

Documentation improvements are some of the most impactful contributions possible. If something confused you, it will confuse the next person too. Fixes welcome via PR — even small ones like correcting a typo or clarifying a confusing sentence.

Documentation lives in `installer/documentation/` as standalone HTML files.

### Contributing Code

If you're thinking about a larger code contribution, please **open an issue first** to discuss it. This avoids the frustrating situation where you do significant work and it turns out to conflict with something already in progress.

For small, clearly-scoped fixes (a genuine bug fix, a missing edge case, etc.) feel free to open a PR directly.

---

## Development Setup

### Prerequisites

- [PlatformIO](https://platformio.org/) (recommended) or the Arduino IDE with ESP32 board support
- Python 3.x (for any tooling scripts)
- Git

### Getting Started

```bash
git clone https://github.com/SpiritWrestlerSolutions/iWetMyPlants.git
cd iWetMyPlants
```

Open the relevant firmware directory in PlatformIO. Each device role (Hub, Remote, Greenhouse) is a separate PlatformIO project with its own `platformio.ini`.

### Building Firmware

```bash
cd firmware/hub
pio run
```

### Flashing for Development

```bash
pio run --target upload
```

Serial monitor:

```bash
pio device monitor --baud 115200
```

---

## Pull Request Process

1. **Fork** the repository and create a branch from `main`
2. **Name your branch** descriptively: `fix/hub-mqtt-reconnect` or `feature/remote-multi-sensor`
3. **Make your changes** — keep commits focused and well-described
4. **Test your changes** on real hardware if at all possible. Simulator testing is acceptable for logic-only changes
5. **Update documentation** if your change affects behaviour visible to end users
6. **Open a pull request** with a clear description of what changed and why
7. **Be patient** — this is a one-person project and review may take a few days

### PR Checklist

- [ ] Changes are tested on physical hardware (or noted as untested)
- [ ] Documentation updated if behaviour changed
- [ ] No hardcoded credentials, network addresses, or personal information
- [ ] Code compiles without warnings on the target platform(s)

---

## Style Guidelines

### C++ (Firmware)

- Follow the existing code style — consistency matters more than any particular convention
- Keep functions focused and reasonably short
- Comment anything that isn't immediately obvious, especially hardware-specific behaviour
- Avoid dynamic memory allocation where possible (this runs on microcontrollers with limited RAM)
- Use `#define` constants for pin numbers, timing values, and other magic numbers

### HTML / CSS / JavaScript (Installer & Docs)

- The installer page (`index.html`) is intentionally self-contained (inline styles and scripts) for reliability
- Documentation pages share a common CSS pattern — match the existing look and feel
- Prefer vanilla JS over libraries; the installer is a single-file tool, not an app framework
- Test in Chrome and Edge at minimum (Web Serial requires Chromium)

### Commit Messages

Use the imperative mood in the subject line:
- `Fix MQTT reconnect loop on Hub`
- `Add ADS1115 support to Remote`
- `Fixed the thing`
- `Changes`

---

## Questions?

If you're not sure whether something is a good fit, just ask. Open an issue tagged `question`, or use the feedback button on the installer page.

Thanks again for taking the time to contribute.
— Brendan
