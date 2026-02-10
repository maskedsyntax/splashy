<div align="center">
  <img width="256" height="256" alt="splashy-wobg" src="https://github.com/user-attachments/assets/740e9b85-354e-43db-9175-cc4e5a73f9c6" />  
  
  <h1>Splashy (C Version)</h1> 
  
  **Splashy** is a lightweight and feature-rich whiteboard application for Linux, rewritten in **C** using **GTK3** and **Cairo** for maximum performance and responsiveness.
  It is designed for quick sketches, diagrams, and professional handwritten notes.
</div>

---

## Features

### Current
- **High Performance:** Native C implementation for ultra-low latency.
- **Stylus Support:** Pressure sensitivity support for Wacom and other professional styluses.
- **Smooth Drawing:** Midpoint quadratic Bézier interpolation for fluid, professional lines.
- **Undo/Redo:** 20-step history stack for all drawing actions.
- **Export:** Save your canvas as high-quality PNG images.
- **Tools:** Pen, Eraser, Line, Rectangle, Circle, Arrow, and Text.
- **Colors:** Deep color palette and custom color picker.
- **Resizable:** Persistent canvas state during window resizing.

### Planned
- **Infinite Canvas:** Horizontal and vertical scaling/panning for unlimited workspace.
- **Page Backgrounds:** Switch between Grid, Lined, Dotted, and Plain backgrounds.
- **Layer Management:** Background vs. drawing layers for complex illustrations.
- **Keyboard Shortcuts:** Global hotkeys for Undo (Ctrl+Z), Redo (Ctrl+Y), Save (Ctrl+S), and Tool selection.

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
```bash
make
./build/splashy
```

---

## Contributions

Contributions are welcome! Fork the repo, create a branch, and open a pull request.

---

## License

This project is licensed under the [**MIT License**](./LICENSE).
