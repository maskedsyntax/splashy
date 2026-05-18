<div align="center">
  <img width="256" height="256" alt="Splashy Logo" src="logo.png" />  
  
  <h1>Splashy</h1> 
  
  **Splashy** is a lightweight and feature-rich whiteboard application for Linux, built with **GTK3** and **Cairo** for maximum performance and responsiveness.
  It is designed for quick sketches, diagrams, and professional handwritten notes.
</div>

---

## Features

- **High Performance:** Native C implementation for ultra-low latency.
- **Stylus Support:** Pressure sensitivity support for professional styluses.
- **Smooth Drawing:** Midpoint quadratic Bézier interpolation for fluid lines.
- **Infinite Canvas:** Horizontal and vertical scaling/panning for unlimited workspace.
- **Page Backgrounds:** Switch between Grid, Lined, Dotted, and Plain backgrounds.
- **Layer Management:** Background and Foreground layers for complex illustrations.
- **Undo/Redo:** Comprehensive history stack for all drawing actions.
- **Export:** Save your canvas as high-quality PNG images.
- **Comprehensive Toolset:** Pen, Eraser, Line, Rectangle, Circle, Arrow, and Text.

---

## Getting Started

### Requirements
Ensure you have the following installed:
- `GCC` (Compiler)
- `Make`
- `GTK+ 3.0` development headers
- `Cairo` development headers

On Debian/Ubuntu-based systems, install dependencies with:
```bash
sudo apt install build-essential libgtk-3-dev libcairo2-dev
```

### Build and Run

#### Linux
```bash
make
./build/splashy
```

#### macOS
Ensure you have dependencies installed via Homebrew:
```bash
brew install gtk+3 cairo pkg-config
```
Build the app bundle:
```bash
make macos
```
Run the app:
```bash
open build/Splashy.app
```

---

## Contributions

Contributions are welcome! Fork the repo, create a branch, and open a pull request.

---

## License

This project is licensed under the [**MIT License**](./LICENSE).