<div align="center">
  <img width="256" height="256" alt="splashy-wobg" src="https://github.com/user-attachments/assets/740e9b85-354e-43db-9175-cc4e5a73f9c6" />  
  
  <h1>Splashy (C Version)</h1> 
  
  **Splashy** is a lightweight and feature-rich whiteboard application for Linux, rewritten in **C** using **GTK3** and **Cairo** for maximum performance and responsiveness.
  It is designed for quick sketches, diagrams, and personal drawing needs.
</div>

---

## Features

### Current
- **High Performance:** Native C implementation for low latency.
- **Stylus Support:** Pressure sensitivity support for Wacom and other styluses.
- **Smooth Drawing:** Advanced input smoothing algorithms for fluid lines.
- Freehand drawing with variable brush size.
- Eraser tool with adjustable size.
- Shape tools: `Line`, `Rectangle`, `Circle`.
- Color palette with a wide selection of preset colors.
- Custom color picker for both drawing and background.
- Clear canvas with a single click.
- Resizable window with persistent canvas state during resize.

### Planned
- Save & Export: Export the canvas as an image file.
- Undo/Redo: Step through previous actions.
- Additional shape tools (polygons, arrows, text).
- Layer management (background vs. drawing layers).

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
Clone the repository and build the app:
```bash
git clone https://github.com/maskedsyntax/splashy
cd splashy
make
./splashy_c
```

---

## Contributions

Contributions are welcome! Fork the repo, create a branch, and open a pull request.

---

## License

This project is licensed under the [**MIT License**](./LICENSE).