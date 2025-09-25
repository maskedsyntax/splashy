#!/usr/bin/env python3
"""
Splashy - A minimal and lightweight whiteboard application for Linux
Built with GTK3 and Cairo for smooth drawing experience
"""

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gdk, GdkPixbuf
import cairo
import math

class DrawingArea(Gtk.DrawingArea):
    def __init__(self):
        super().__init__()
        
        # Drawing state
        self.drawing = False
        self.last_x = 0
        self.last_y = 0
        self.current_color = (0, 0, 0, 1)  # RGBA - Black by default
        self.brush_size = 3
        
        # Surface for persistent drawing
        self.surface = None
        
        # Connect drawing events
        self.connect('draw', self.on_draw)
        self.connect('configure-event', self.on_configure)
        
        # Mouse events
        self.connect('button-press-event', self.on_button_press)
        self.connect('button-release-event', self.on_button_release)
        self.connect('motion-notify-event', self.on_motion_notify)
        
        # Enable mouse events
        self.set_events(
            Gdk.EventMask.BUTTON_PRESS_MASK |
            Gdk.EventMask.BUTTON_RELEASE_MASK |
            Gdk.EventMask.POINTER_MOTION_MASK
        )

    def on_configure(self, widget, event):
        """Configure the cairo surface when window is resized"""
        allocation = widget.get_allocation()
        
        # Create new surface
        self.surface = cairo.ImageSurface(
            cairo.FORMAT_ARGB32,
            allocation.width,
            allocation.height
        )
        
        # Fill with white background
        ctx = cairo.Context(self.surface)
        ctx.set_source_rgb(1, 1, 1)  # White
        ctx.paint()
        
        return True

    def on_draw(self, widget, ctx):
        """Draw the surface content"""
        if self.surface:
            ctx.set_source_surface(self.surface)
            ctx.paint()
        return False

    def on_button_press(self, widget, event):
        """Handle mouse button press"""
        if event.button == 1:  # Left mouse button
            self.drawing = True
            self.last_x = event.x
            self.last_y = event.y
        return True

    def on_button_release(self, widget, event):
        """Handle mouse button release"""
        if event.button == 1:  # Left mouse button
            self.drawing = False
        return True

    def on_motion_notify(self, widget, event):
        """Handle mouse motion for drawing"""
        if self.drawing and self.surface:
            # Draw on the surface
            ctx = cairo.Context(self.surface)
            ctx.set_source_rgba(*self.current_color)
            ctx.set_line_cap(cairo.LineCap.ROUND)
            ctx.set_line_join(cairo.LineJoin.ROUND)
            ctx.set_line_width(self.brush_size)
            
            # Draw line from last position to current position
            ctx.move_to(self.last_x, self.last_y)
            ctx.line_to(event.x, event.y)
            ctx.stroke()
            
            # Update last position
            self.last_x = event.x
            self.last_y = event.y
            
            # Queue redraw
            self.queue_draw()
        
        return True

    def clear_canvas(self):
        """Clear the entire canvas"""
        if self.surface:
            ctx = cairo.Context(self.surface)
            ctx.set_source_rgb(1, 1, 1)  # White background
            ctx.paint()
            self.queue_draw()

    def set_color(self, color):
        """Set the drawing color (RGBA tuple)"""
        self.current_color = color

    def set_brush_size(self, size):
        """Set the brush size"""
        self.brush_size = size


class SplashyWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title="Splashy - Whiteboard")
        
        # Window setup
        self.set_default_size(800, 600)
        self.set_position(Gtk.WindowPosition.CENTER)
        
        # Create main layout
        main_box = Gtk.VBox()
        self.add(main_box)
        
        # Create toolbar
        toolbar = self.create_toolbar()
        main_box.pack_start(toolbar, False, False, 0)
        
        # Create drawing area
        self.drawing_area = DrawingArea()
        main_box.pack_start(self.drawing_area, True, True, 0)
        
        # Connect window close event
        self.connect('destroy', Gtk.main_quit)

    def create_toolbar(self):
        """Create the application toolbar"""
        toolbar = Gtk.Toolbar()
        toolbar.set_style(Gtk.ToolbarStyle.BOTH_HORIZ)
        
        # Clear button
        clear_btn = Gtk.ToolButton()
        clear_btn.set_label("Clear")
        clear_btn.set_icon_name("edit-clear")
        clear_btn.connect('clicked', self.on_clear_clicked)
        toolbar.insert(clear_btn, -1)
        
        # Separator
        sep1 = Gtk.SeparatorToolItem()
        toolbar.insert(sep1, -1)
        
        # Color palette
        colors = [
            ("Black", (0, 0, 0, 1)),
            ("Red", (1, 0, 0, 1)),
            ("Blue", (0, 0, 1, 1)),
            ("Green", (0, 0.7, 0, 1))
        ]
        
        for color_name, color_rgba in colors:
            color_btn = self.create_color_button(color_name, color_rgba)
            toolbar.insert(color_btn, -1)
        
        # Separator
        sep2 = Gtk.SeparatorToolItem()
        toolbar.insert(sep2, -1)
        
        # Brush size
        brush_label = Gtk.ToolItem()
        brush_label.add(Gtk.Label(label="Brush Size:"))
        toolbar.insert(brush_label, -1)
        
        brush_adjustment = Gtk.Adjustment(
            value=3, lower=1, upper=20, 
            step_increment=1, page_increment=5
        )
        brush_scale = Gtk.Scale(
            orientation=Gtk.Orientation.HORIZONTAL,
            adjustment=brush_adjustment
        )
        brush_scale.set_digits(0)
        brush_scale.set_size_request(100, -1)
        brush_scale.connect('value-changed', self.on_brush_size_changed)
        
        brush_item = Gtk.ToolItem()
        brush_item.add(brush_scale)
        toolbar.insert(brush_item, -1)
        
        return toolbar

    def create_color_button(self, name, color_rgba):
        """Create a color selection button"""
        btn = Gtk.ToolButton()
        btn.set_label(name)
        
        # Create colored icon
        pixbuf = GdkPixbuf.Pixbuf.new(
            GdkPixbuf.Colorspace.RGB, True, 8, 24, 24
        )
        # Fill with color (convert RGBA to RGB with alpha)
        r, g, b, a = color_rgba
        color_int = (int(r * 255) << 24) | (int(g * 255) << 16) | (int(b * 255) << 8) | int(a * 255)
        pixbuf.fill(color_int)
        
        icon = Gtk.Image.new_from_pixbuf(pixbuf)
        btn.set_icon_widget(icon)
        
        btn.connect('clicked', lambda x: self.on_color_clicked(color_rgba))
        return btn

    def on_clear_clicked(self, button):
        """Handle clear button click"""
        self.drawing_area.clear_canvas()

    def on_color_clicked(self, color):
        """Handle color button click"""
        self.drawing_area.set_color(color)

    def on_brush_size_changed(self, scale):
        """Handle brush size change"""
        size = int(scale.get_value())
        self.drawing_area.set_brush_size(size)


class SplashyApp:
    def __init__(self):
        self.window = SplashyWindow()

    def run(self):
        """Run the application"""
        self.window.show_all()
        Gtk.main()


if __name__ == '__main__':
    app = SplashyApp()
    app.run()