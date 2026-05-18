<div align="center">
  <img src="logo.png" width="120" height="120" alt="Splashy Logo" />

  # Splashy
  
  **The lightweight, high-performance digital whiteboard for Linux & macOS.**

  [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
  [![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS-blue.svg)](#)
  [![C](https://img.shields.io/badge/Language-C-00599C.svg)](#)

  [Features](#-features) • [Installation](#-installation) • [Keybindings](#-keybindings) • [License](#-license)
</div>

---

## Features

- **Performance-First:** Native C & Cairo implementation for near-zero latency drawing.
- **Stylus Optimized:** Full pressure sensitivity support for professional tablets.
- **Fluid Lines:** Midpoint quadratic Bézier interpolation for smooth, natural strokes.
- **macOS Native:** Integrated with SF Symbols and tailored for Retina displays.
- **Layer System:** Organize your work with multiple layers and adjustable transparency.
- **Perfect Geometry:** Snap-to-grid shapes (Line, Circle, Star, Triangle, Arrow).
- **Multiple Backgrounds:** Grid, Lined, Dotted, or Plain canvas styles.
- **File Formats:** Save projects as .sphy or export to high-quality PNG and PDF.

---

## Installation

### macOS
**Prerequisites:** [Homebrew](https://brew.sh/)
```bash
brew install gtk+3 cairo pkg-config
make macos
open build/Splashy.app
```

### Linux
**Prerequisites (Debian/Ubuntu):**
```bash
sudo apt install build-essential libgtk-3-dev libcairo2-dev
make
./build/splashy
```

---

## Keybindings

| Shortcut | Action |
| :--- | :--- |
| `Cmd/Ctrl + Z` | Undo |
| `Cmd/Ctrl + Shift + Z` | Redo |
| `Cmd/Ctrl + S` | Save Project (`.sphy`) |
| `Cmd/Ctrl + E` | Export as PNG |
| `Cmd/Ctrl + O` | Open Project |
| `Scroll` | Pan Canvas |
| `Cmd/Ctrl + Scroll` | Zoom In/Out |

---

## Contributing

Contributions make the open-source community an amazing place to learn, inspire, and create. Any contributions you make are **greatly appreciated**.

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

---

## License

Distributed under the MIT License. See `LICENSE` for more information.

Built by [maskedsyntax](https://github.com/maskedsyntax)
