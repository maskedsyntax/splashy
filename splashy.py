#!/usr/bin/env python3
"""
Splashy - A fully-featured whiteboard application for Linux
Built with GTK3 and Cairo with comprehensive drawing tools
"""

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Gdk, GdkPixbuf, GObject
import cairo
import math


class DrawingTool:
    """Enumeration for drawing tools"""

    PEN = "pen"
    ERASER = "eraser"
    LINE = "line"
    RECTANGLE = "rectangle"
    CIRCLE = "circle"


class DrawingArea(Gtk.DrawingArea):
    def __init__(self):
        super().__init__()

        # Drawing state
        self.drawing = False
        self.current_tool = DrawingTool.PEN
        self.start_x = 0
        self.start_y = 0
        self.last_x = 0
        self.last_y = 0
        self.current_color = (0, 0, 0, 1)  # RGBA - Black by default
        self.background_color = (1, 1, 1, 1)  # White background
        self.brush_size = 3
        self.eraser_size = 10

        # Surface for persistent drawing
        self.surface = None
        self.temp_surface = None  # For preview shapes

        # Connect drawing events
        self.connect("draw", self.on_draw)
        self.connect("configure-event", self.on_configure)

        # Mouse events
        self.connect("button-press-event", self.on_button_press)
        self.connect("button-release-event", self.on_button_release)
        self.connect("motion-notify-event", self.on_motion_notify)

        # Enable mouse events
        self.set_events(
            Gdk.EventMask.BUTTON_PRESS_MASK
            | Gdk.EventMask.BUTTON_RELEASE_MASK
            | Gdk.EventMask.POINTER_MOTION_MASK
        )

    def on_configure(self, widget, event):
        """Configure the cairo surface when window is resized"""
        allocation = widget.get_allocation()
        new_width, new_height = allocation.width, allocation.height

        # If no surface exists yet, create one
        if self.surface is None:
            self.surface = cairo.ImageSurface(
                cairo.FORMAT_ARGB32, new_width, new_height
            )
            self.temp_surface = cairo.ImageSurface(
                cairo.FORMAT_ARGB32, new_width, new_height
            )
            ctx = cairo.Context(self.surface)
            ctx.set_source_rgba(*self.background_color)
            ctx.paint()
            return True

        old_width = self.surface.get_width()
        old_height = self.surface.get_height()

        # Only create a new surface if the window is larger than before
        if new_width > old_width or new_height > old_height:
            # Grow to the new size
            new_surface = cairo.ImageSurface(
                cairo.FORMAT_ARGB32,
                max(new_width, old_width),
                max(new_height, old_height),
            )

            ctx = cairo.Context(new_surface)
            ctx.set_source_rgba(*self.background_color)
            ctx.paint()
            ctx.set_source_surface(self.surface, 0, 0)
            ctx.paint()

            self.surface = new_surface

            # Same for temp surface
            self.temp_surface = cairo.ImageSurface(
                cairo.FORMAT_ARGB32,
                max(new_width, old_width),
                max(new_height, old_height),
            )

        return True

    def on_draw(self, widget, ctx):
        """Draw the surface content"""
        if self.surface:
            ctx.set_source_surface(self.surface)
            ctx.paint()

            # Draw temporary surface on top (for shape previews)
            if self.temp_surface:
                ctx.set_source_surface(self.temp_surface)
                ctx.paint()
        return False

    def on_button_press(self, widget, event):
        """Handle mouse button press"""
        if event.button == 1:  # Left mouse button
            self.drawing = True
            self.start_x = event.x
            self.start_y = event.y
            self.last_x = event.x
            self.last_y = event.y

            # Clear temp surface for new shape
            if self.current_tool in [
                DrawingTool.LINE,
                DrawingTool.RECTANGLE,
                DrawingTool.CIRCLE,
            ]:
                self.clear_temp_surface()
        return True

    def on_button_release(self, widget, event):
        """Handle mouse button release"""
        if event.button == 1 and self.drawing:
            self.drawing = False

            # For shapes, commit the preview to main surface
            if self.current_tool in [
                DrawingTool.LINE,
                DrawingTool.RECTANGLE,
                DrawingTool.CIRCLE,
            ]:
                self.commit_shape(event.x, event.y)
                self.clear_temp_surface()

        return True

    def on_motion_notify(self, widget, event):
        """Handle mouse motion for drawing"""
        if self.drawing and self.surface:
            if self.current_tool == DrawingTool.PEN:
                self.draw_freehand(event.x, event.y)
            elif self.current_tool == DrawingTool.ERASER:
                self.erase(event.x, event.y)
            elif self.current_tool in [
                DrawingTool.LINE,
                DrawingTool.RECTANGLE,
                DrawingTool.CIRCLE,
            ]:
                self.preview_shape(event.x, event.y)

        return True

    def draw_freehand(self, x, y):
        """Draw freehand lines"""
        ctx = cairo.Context(self.surface)
        ctx.set_source_rgba(*self.current_color)
        ctx.set_line_cap(cairo.LineCap.ROUND)
        ctx.set_line_join(cairo.LineJoin.ROUND)
        ctx.set_line_width(self.brush_size)

        ctx.move_to(self.last_x, self.last_y)
        ctx.line_to(x, y)
        ctx.stroke()

        self.last_x = x
        self.last_y = y
        self.queue_draw()

    def erase(self, x, y):
        """Erase with background color"""
        ctx = cairo.Context(self.surface)
        ctx.set_source_rgba(*self.background_color)
        ctx.set_line_cap(cairo.LineCap.ROUND)
        ctx.set_line_join(cairo.LineJoin.ROUND)
        ctx.set_line_width(self.eraser_size)

        ctx.move_to(self.last_x, self.last_y)
        ctx.line_to(x, y)
        ctx.stroke()

        self.last_x = x
        self.last_y = y
        self.queue_draw()

    def preview_shape(self, end_x, end_y):
        """Preview shape while dragging"""
        self.clear_temp_surface()
        ctx = cairo.Context(self.temp_surface)
        ctx.set_source_rgba(*self.current_color)
        ctx.set_line_width(self.brush_size)

        if self.current_tool == DrawingTool.LINE:
            ctx.move_to(self.start_x, self.start_y)
            ctx.line_to(end_x, end_y)
            ctx.stroke()
        elif self.current_tool == DrawingTool.RECTANGLE:
            width = end_x - self.start_x
            height = end_y - self.start_y
            ctx.rectangle(self.start_x, self.start_y, width, height)
            ctx.stroke()
        elif self.current_tool == DrawingTool.CIRCLE:
            radius = math.sqrt(
                (end_x - self.start_x) ** 2 + (end_y - self.start_y) ** 2
            )
            ctx.arc(self.start_x, self.start_y, radius, 0, 2 * math.pi)
            ctx.stroke()

        self.queue_draw()

    def commit_shape(self, end_x, end_y):
        """Commit shape to main surface"""
        ctx = cairo.Context(self.surface)
        ctx.set_source_rgba(*self.current_color)
        ctx.set_line_width(self.brush_size)

        if self.current_tool == DrawingTool.LINE:
            ctx.move_to(self.start_x, self.start_y)
            ctx.line_to(end_x, end_y)
            ctx.stroke()
        elif self.current_tool == DrawingTool.RECTANGLE:
            width = end_x - self.start_x
            height = end_y - self.start_y
            ctx.rectangle(self.start_x, self.start_y, width, height)
            ctx.stroke()
        elif self.current_tool == DrawingTool.CIRCLE:
            radius = math.sqrt(
                (end_x - self.start_x) ** 2 + (end_y - self.start_y) ** 2
            )
            ctx.arc(self.start_x, self.start_y, radius, 0, 2 * math.pi)
            ctx.stroke()

    def clear_temp_surface(self):
        """Clear the temporary preview surface"""
        if self.temp_surface:
            ctx = cairo.Context(self.temp_surface)
            ctx.set_operator(cairo.Operator.CLEAR)
            ctx.paint()

    def clear_canvas(self):
        """Clear the entire canvas"""
        if self.surface:
            ctx = cairo.Context(self.surface)
            ctx.set_source_rgba(*self.background_color)
            ctx.paint()
            self.queue_draw()

    def set_tool(self, tool):
        """Set the current drawing tool"""
        self.current_tool = tool

    def set_color(self, color):
        """Set the drawing color (RGBA tuple)"""
        self.current_color = color

    def set_background_color(self, color):
        """Set the background color"""
        self.background_color = color
        self.clear_canvas()

    def set_brush_size(self, size):
        """Set the brush size"""
        self.brush_size = size

    def set_eraser_size(self, size):
        """Set the eraser size"""
        self.eraser_size = size


class ColorPalette(Gtk.Grid):
    def __init__(self, callback):
        super().__init__()
        self.callback = callback

        # Predefined color palette
        colors = [
            # Row 1 - Basic colors
            [(0, 0, 0, 1), (0.4, 0.4, 0.4, 1), (0.6, 0.6, 0.6, 1), (1, 1, 1, 1)],
            # Row 2 - Reds
            [(1, 0, 0, 1), (0.8, 0, 0, 1), (0.6, 0, 0, 1), (1, 0.4, 0.4, 1)],
            # Row 3 - Blues
            [(0, 0, 1, 1), (0, 0, 0.8, 1), (0, 0, 0.6, 1), (0.4, 0.4, 1, 1)],
            # Row 4 - Greens
            [(0, 0.8, 0, 1), (0, 0.6, 0, 1), (0, 0.4, 0, 1), (0.4, 1, 0.4, 1)],
            # Row 5 - Yellows/Oranges
            [(1, 1, 0, 1), (1, 0.8, 0, 1), (1, 0.6, 0, 1), (1, 0.4, 0, 1)],
            # Row 6 - Purples/Pinks
            [(0.8, 0, 0.8, 1), (0.6, 0, 0.6, 1), (1, 0.4, 1, 1), (0.8, 0.4, 0.8, 1)],
        ]

        self.set_column_spacing(2)
        self.set_row_spacing(2)

        for row, color_row in enumerate(colors):
            for col, color in enumerate(color_row):
                btn = self.create_color_button(color)
                self.attach(btn, col, row, 1, 1)

    def create_color_button(self, color_rgba):
        """Create a color button"""
        btn = Gtk.Button()
        btn.set_size_request(25, 25)

        # Set button color
        r, g, b, a = color_rgba
        css = f"button {{ background-color: rgba({int(r*255)}, {int(g*255)}, {int(b*255)}, {a}); }}"

        provider = Gtk.CssProvider()
        provider.load_from_data(css.encode())
        btn.get_style_context().add_provider(provider, Gtk.STYLE_PROVIDER_PRIORITY_USER)

        btn.connect("clicked", lambda x: self.callback(color_rgba))
        return btn


class SplashyWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title="Splashy - Advanced Whiteboard")

        # Window setup
        self.set_default_size(1000, 700)
        self.set_position(Gtk.WindowPosition.CENTER)

        # Create drawing area first (needed by sidebar)
        self.drawing_area = DrawingArea()

        # Create main layout (horizontal box)
        main_box = Gtk.HBox()
        self.add(main_box)

        # Create left sidebar
        sidebar = self.create_sidebar()
        main_box.pack_start(sidebar, False, False, 0)

        # Add separator
        separator = Gtk.VSeparator()
        main_box.pack_start(separator, False, False, 5)

        # Add drawing area to layout
        main_box.pack_start(self.drawing_area, True, True, 0)

        # Connect window close event
        self.connect("destroy", Gtk.main_quit)

    def create_sidebar(self):
        """Create the left sidebar with all tools"""
        sidebar = Gtk.VBox(spacing=10)
        sidebar.set_size_request(200, -1)
        sidebar.set_margin_start(10)
        sidebar.set_margin_end(10)
        sidebar.set_margin_top(10)
        sidebar.set_margin_bottom(10)

        # Tools section
        tools_frame = Gtk.Frame(label="Tools")
        tools_box = Gtk.VBox(spacing=5)
        tools_box.set_margin_start(10)
        tools_box.set_margin_end(10)
        tools_box.set_margin_top(10)
        tools_box.set_margin_bottom(10)

        # Tool buttons
        self.tool_buttons = {}
        tools = [
            ("Pen", DrawingTool.PEN),
            ("Eraser", DrawingTool.ERASER),
            ("Line", DrawingTool.LINE),
            ("Rectangle", DrawingTool.RECTANGLE),
            ("Circle", DrawingTool.CIRCLE),
        ]

        for name, tool in tools:
            btn = Gtk.ToggleButton(label=f"{name}")
            btn.connect("toggled", lambda x, t=tool: self.on_tool_selected(t))
            self.tool_buttons[tool] = btn
            tools_box.pack_start(btn, False, False, 0)

        # Set pen as default
        self.tool_buttons[DrawingTool.PEN].set_active(True)

        tools_frame.add(tools_box)
        sidebar.pack_start(tools_frame, False, False, 0)

        # Brush size section
        brush_frame = Gtk.Frame(label="Brush Size")
        brush_box = Gtk.VBox(spacing=5)
        brush_box.set_margin_start(10)
        brush_box.set_margin_end(10)
        brush_box.set_margin_top(10)
        brush_box.set_margin_bottom(10)

        self.brush_scale = Gtk.Scale(
            orientation=Gtk.Orientation.HORIZONTAL,
            adjustment=Gtk.Adjustment(value=3, lower=1, upper=20, step_increment=1),
        )
        self.brush_scale.set_digits(0)
        self.brush_scale.connect("value-changed", self.on_brush_size_changed)
        brush_box.pack_start(Gtk.Label(label="Pen Size"), False, False, 0)
        brush_box.pack_start(self.brush_scale, False, False, 0)

        self.eraser_scale = Gtk.Scale(
            orientation=Gtk.Orientation.HORIZONTAL,
            adjustment=Gtk.Adjustment(value=10, lower=5, upper=50, step_increment=5),
        )
        self.eraser_scale.set_digits(0)
        self.eraser_scale.connect("value-changed", self.on_eraser_size_changed)
        brush_box.pack_start(Gtk.Label(label="Eraser Size"), False, False, 0)
        brush_box.pack_start(self.eraser_scale, False, False, 0)

        brush_frame.add(brush_box)
        sidebar.pack_start(brush_frame, False, False, 0)

        # Colors section
        colors_frame = Gtk.Frame(label="Colors")
        colors_box = Gtk.VBox(spacing=5)
        colors_box.set_margin_start(10)
        colors_box.set_margin_end(10)
        colors_box.set_margin_top(10)
        colors_box.set_margin_bottom(10)

        color_palette = ColorPalette(self.on_color_selected)
        colors_box.pack_start(color_palette, False, False, 0)

        # Custom color button
        custom_color_btn = Gtk.Button(label="Custom Color...")
        custom_color_btn.connect("clicked", self.on_custom_color_clicked)
        colors_box.pack_start(custom_color_btn, False, False, 0)

        colors_frame.add(colors_box)
        sidebar.pack_start(colors_frame, False, False, 0)

        # Background section
        bg_frame = Gtk.Frame(label="Background")
        bg_box = Gtk.VBox(spacing=5)
        bg_box.set_margin_start(10)
        bg_box.set_margin_end(10)
        bg_box.set_margin_top(10)
        bg_box.set_margin_bottom(10)

        bg_color_btn = Gtk.Button(label="Background Color...")
        bg_color_btn.connect("clicked", self.on_background_color_clicked)
        bg_box.pack_start(bg_color_btn, False, False, 0)

        bg_frame.add(bg_box)
        sidebar.pack_start(bg_frame, False, False, 0)

        # Actions section
        actions_frame = Gtk.Frame(label="Actions")
        actions_box = Gtk.VBox(spacing=5)
        actions_box.set_margin_start(10)
        actions_box.set_margin_end(10)
        actions_box.set_margin_top(10)
        actions_box.set_margin_bottom(10)

        clear_btn = Gtk.Button(label="🗑️ Clear Canvas")
        clear_btn.connect("clicked", self.on_clear_clicked)
        actions_box.pack_start(clear_btn, False, False, 0)

        actions_frame.add(actions_box)
        sidebar.pack_start(actions_frame, False, False, 0)

        return sidebar

    def on_tool_selected(self, tool):
        """Handle tool selection"""
        # Unset other toggle buttons
        for t, btn in self.tool_buttons.items():
            if t != tool:
                btn.set_active(False)

        self.drawing_area.set_tool(tool)

    def on_brush_size_changed(self, scale):
        """Handle brush size change"""
        size = int(scale.get_value())
        self.drawing_area.set_brush_size(size)

    def on_eraser_size_changed(self, scale):
        """Handle eraser size change"""
        size = int(scale.get_value())
        self.drawing_area.set_eraser_size(size)

    def on_color_selected(self, color):
        """Handle color selection"""
        self.drawing_area.set_color(color)

    def on_custom_color_clicked(self, button):
        """Open custom color picker"""
        dialog = Gtk.ColorChooserDialog(title="Choose Color", parent=self)
        response = dialog.run()

        if response == Gtk.ResponseType.OK:
            color = dialog.get_rgba()
            rgba_color = (color.red, color.green, color.blue, color.alpha)
            self.drawing_area.set_color(rgba_color)

        dialog.destroy()

    def on_background_color_clicked(self, button):
        """Open background color picker"""
        dialog = Gtk.ColorChooserDialog(title="Choose Background Color", parent=self)
        response = dialog.run()

        if response == Gtk.ResponseType.OK:
            color = dialog.get_rgba()
            rgba_color = (color.red, color.green, color.blue, color.alpha)
            self.drawing_area.set_background_color(rgba_color)

        dialog.destroy()

    def on_clear_clicked(self, button):
        """Handle clear button click"""
        self.drawing_area.clear_canvas()


class SplashyApp:
    def __init__(self):
        self.window = SplashyWindow()

    def run(self):
        """Run the application"""
        self.window.show_all()
        Gtk.main()


if __name__ == "__main__":
    app = SplashyApp()
    app.run()
