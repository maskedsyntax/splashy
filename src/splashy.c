#include <gtk/gtk.h>
#include <cairo.h>
#include <math.h>
#include <stdlib.h>

// --- Constants & Enums ---

typedef enum {
    TOOL_PEN,
    TOOL_ERASER,
    TOOL_LINE,
    TOOL_RECTANGLE,
    TOOL_CIRCLE,
    TOOL_ARROW,
    TOOL_TEXT
} ToolType;

// --- Data Structures ---

#define MAX_UNDO 20

typedef struct {
    double r, g, b, a;
} Color;

typedef struct {
    double x, y;
    double pressure;
} Point;

typedef enum {
    PAGE_PLAIN,
    PAGE_GRID,
    PAGE_LINED,
    PAGE_DOTTED
} PageType;

typedef struct {
    GtkWidget *window;
    GtkWidget *drawing_area;
    
    // UI References
    GtkWidget *brush_scale;
    GtkWidget *eraser_scale;
    GtkWidget *tool_buttons[7]; // Indexed by ToolType

    // Cairo Surfaces
    cairo_surface_t *layers[2];    // 0: Background, 1: Foreground
    int current_layer;
    cairo_surface_t *surface;      // Points to layers[current_layer]
    cairo_surface_t *temp_surface; // Preview surface for shapes

    // Undo/Redo History
    cairo_surface_t *undo_stack[MAX_UNDO];
    int history_index; // points to current state
    int history_max;   // points to the top of available redo states

    // State
    ToolType current_tool;
    PageType current_page_type;
    Color current_color;
    Color background_color;
    double brush_size;
    double eraser_size;
    gboolean drawing;
    
    // Canvas Transformation
    double offset_x, offset_y;
    double scale;
    gboolean panning;
    double last_pan_x, last_pan_y;

    // Input State
    Point start_point; // For shapes
    
    // Smoothing / Interpolation Buffer
    Point points[4];
    int point_count;
    
} AppState;

// --- Global State (or pass via user_data) ---
// We will pass AppState* as user_data to callbacks.

// --- Forward Declarations ---

static void clear_surface(cairo_surface_t *surface, Color col);
static void save_history(AppState *app);

// --- History Management ---

static void save_history(AppState *app) {
    if (!app->surface) return;

    // If we have redo states, they are now invalidated
    for (int i = app->history_index + 1; i <= app->history_max && i < MAX_UNDO; i++) {
        if (app->undo_stack[i]) {
            cairo_surface_destroy(app->undo_stack[i]);
            app->undo_stack[i] = NULL;
        }
    }

    // Shift stack if full
    if (app->history_index == MAX_UNDO - 1) {
        if (app->undo_stack[0]) cairo_surface_destroy(app->undo_stack[0]);
        for (int i = 0; i < MAX_UNDO - 1; i++) {
            app->undo_stack[i] = app->undo_stack[i+1];
        }
        app->history_index--;
    }

    app->history_index++;
    int w = cairo_image_surface_get_width(app->surface);
    int h = cairo_image_surface_get_height(app->surface);
    app->undo_stack[app->history_index] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    
    cairo_t *cr = cairo_create(app->undo_stack[app->history_index]);
    cairo_set_source_surface(cr, app->surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    app->history_max = app->history_index;
}

static void undo(AppState *app) {
    if (app->history_index > 0) {
        app->history_index--;
        
        cairo_t *cr = cairo_create(app->surface);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_surface(cr, app->undo_stack[app->history_index], 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
        
        gtk_widget_queue_draw(app->drawing_area);
    }
}

static void redo(AppState *app) {
    if (app->history_index < app->history_max) {
        app->history_index++;
        
        cairo_t *cr = cairo_create(app->surface);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_surface(cr, app->undo_stack[app->history_index], 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
        
        gtk_widget_queue_draw(app->drawing_area);
    }
}

static void on_undo_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    undo((AppState *)user_data);
}

static void on_redo_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    redo((AppState *)user_data);
}

// --- Helper Functions ---

static Color make_color(double r, double g, double b, double a) {
    Color c = {r, g, b, a};
    return c;
}

static void update_tool_buttons(AppState *app) {
    for (int i = 0; i < 7; i++) {
        if ((ToolType)i == app->current_tool) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->tool_buttons[i]), TRUE);
        } else {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->tool_buttons[i]), FALSE);
        }
    }
}

// --- Drawing Logic ---

static void ensure_surface(AppState *app, int width, int height, double dx, double dy) {
    if (!app->layers[0]) {
        app->layers[0] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        app->layers[1] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        clear_surface(app->layers[0], app->background_color);
        clear_surface(app->layers[1], app->background_color);
        
        app->surface = app->layers[app->current_layer];
        
        app->temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        // temp surface should be transparent initially
        cairo_t *cr = cairo_create(app->temp_surface);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_destroy(cr);

        // Initial state
        save_history(app);
        return;
    }

    int old_w = cairo_image_surface_get_width(app->layers[0]);
    int old_h = cairo_image_surface_get_height(app->layers[0]);

    if (width > old_w || height > old_h || dx > 0 || dy > 0) {
        int new_w = (width > old_w) ? width : old_w;
        int new_h = (height > old_h) ? height : old_h;

        for (int i = 0; i < 2; i++) {
            cairo_surface_t *new_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_w, new_h);
            clear_surface(new_surf, app->background_color);

            cairo_t *cr = cairo_create(new_surf);
            cairo_set_source_surface(cr, app->layers[i], dx, dy);
            cairo_paint(cr);
            cairo_destroy(cr);

            cairo_surface_destroy(app->layers[i]);
            app->layers[i] = new_surf;
        }
        app->surface = app->layers[app->current_layer];

        // Resize temp surface too
        cairo_surface_destroy(app->temp_surface);
        app->temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_w, new_h);
        cairo_t *cr = cairo_create(app->temp_surface);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_destroy(cr);
    }
}

static void clear_surface(cairo_surface_t *surface, Color col) {
    (void)col;
    cairo_t *cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_destroy(cr);
}

static void clear_temp_surface(AppState *app) {
    cairo_t *cr = cairo_create(app->temp_surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_destroy(cr);
}

// Quadratic Bezier Interpolation for smooth lines
static void draw_smooth_segment(AppState *app, cairo_t *cr) {
    // We need at least 3 points to interpolate between the first two intervals
    // actually, midpoint smoothing uses 3 points to draw from mid1 to mid2.
    if (app->point_count < 3) return;

    Point p0 = app->points[0];
    Point p1 = app->points[1];
    Point p2 = app->points[2];

    // Midpoints
    double mid1_x = (p0.x + p1.x) / 2.0;
    double mid1_y = (p0.y + p1.y) / 2.0;
    double mid2_x = (p1.x + p2.x) / 2.0;
    double mid2_y = (p1.y + p2.y) / 2.0;

    cairo_move_to(cr, mid1_x, mid1_y);
    // Quadratic Bezier: control point is p1, end is mid2.
    // Cairo only has cubic bezier (curve_to).
    // A quadratic Bezier from P0 to P2 with control P1 can be represented as cubic with:
    // CP1 = P0 + 2/3 (P1 - P0)
    // CP2 = P2 + 2/3 (P1 - P2)
    // Here start is mid1, end is mid2, control is p1.
    
    double cp1_x = mid1_x + (2.0/3.0) * (p1.x - mid1_x);
    double cp1_y = mid1_y + (2.0/3.0) * (p1.y - mid1_y);
    double cp2_x = mid2_x + (2.0/3.0) * (p1.x - mid2_x);
    double cp2_y = mid2_y + (2.0/3.0) * (p1.y - mid2_y);

    cairo_curve_to(cr, cp1_x, cp1_y, cp2_x, cp2_y, mid2_x, mid2_y);
    
    cairo_stroke(cr);
}


static void draw_background_pattern(AppState *app, cairo_t *cr, int w, int h) {
    // Fill the visible screen with background color
    // Since cr is already translated/scaled, we need to know the visible bounds in world coords
    double v_x1 = -app->offset_x / app->scale;
    double v_y1 = -app->offset_y / app->scale;
    double v_x2 = v_x1 + w / app->scale;
    double v_y2 = v_y1 + h / app->scale;

    cairo_save(cr);
    cairo_set_source_rgba(cr, app->background_color.r, app->background_color.g, app->background_color.b, app->background_color.a);
    cairo_rectangle(cr, v_x1, v_y1, v_x2 - v_x1, v_y2 - v_y1);
    cairo_fill(cr);
    cairo_restore(cr);

    if (app->current_page_type == PAGE_PLAIN) return;

    cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 0.5);
    cairo_set_line_width(cr, 0.5 / app->scale); // Keep line width constant on screen

    double step = 30.0;
    double start_x = floor(v_x1 / step) * step;
    double start_y = floor(v_y1 / step) * step;

    if (app->current_page_type == PAGE_GRID) {
        for (double x = start_x; x <= v_x2; x += step) {
            cairo_move_to(cr, x, v_y1);
            cairo_line_to(cr, x, v_y2);
        }
        for (double y = start_y; y <= v_y2; y += step) {
            cairo_move_to(cr, v_x1, y);
            cairo_line_to(cr, v_x2, y);
        }
        cairo_stroke(cr);
    } else if (app->current_page_type == PAGE_LINED) {
        for (double y = start_y; y <= v_y2; y += step) {
            cairo_move_to(cr, v_x1, y);
            cairo_line_to(cr, v_x2, y);
        }
        cairo_stroke(cr);
    } else if (app->current_page_type == PAGE_DOTTED) {
        for (double x = start_x; x < v_x2; x += step) {
            for (double y = start_y; y < v_y2; y += step) {
                cairo_arc(cr, x, y, 1.0 / app->scale, 0, 2 * M_PI);
                cairo_fill(cr);
            }
        }
    }
}

// --- Event Callbacks ---

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    // Background pattern is NOT transformed (stays fixed to screen)
    // Actually, usually whiteboard grids should pan WITH the drawing.
    // Let's transform it too.
    
    cairo_save(cr);
    cairo_translate(cr, app->offset_x, app->offset_y);
    cairo_scale(cr, app->scale, app->scale);

    draw_background_pattern(app, cr, allocation.width / app->scale + 200, allocation.height / app->scale + 200); // Draw enough to cover

    for (int i = 0; i < 2; i++) {
        if (app->layers[i]) {
            cairo_set_source_surface(cr, app->layers[i], 0, 0);
            cairo_paint(cr);
        }
    }
    
    if (app->temp_surface) {
        cairo_set_source_surface(cr, app->temp_surface, 0, 0);
        cairo_paint(cr);
    }
    cairo_restore(cr);

    return FALSE;
}

static gboolean on_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer user_data) {
    (void)widget;
    AppState *app = (AppState *)user_data;
    ensure_surface(app, event->width, event->height, 0, 0);
    return TRUE;
}

static gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    double zoom_factor = 1.1;
    if (event->direction == GDK_SCROLL_DOWN) zoom_factor = 1.0 / 1.1;
    else if (event->direction == GDK_SCROLL_UP) zoom_factor = 1.1;
    else if (event->direction == GDK_SCROLL_SMOOTH) {
        double delta_x, delta_y;
        gdk_event_get_scroll_deltas((GdkEvent*)event, &delta_x, &delta_y);
        if (delta_y > 0) zoom_factor = 1.0 / 1.1;
        else zoom_factor = 1.1;
    }

    app->scale *= zoom_factor;
    
    // Zoom towards mouse position
    // New offset should satisfy: (mouse_x - new_offset) / new_scale = (mouse_x - old_offset) / old_scale
    app->offset_x = event->x - (event->x - app->offset_x) * zoom_factor;
    app->offset_y = event->y - (event->y - app->offset_y) * zoom_factor;

    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    
    if (event->button == GDK_BUTTON_MIDDLE) {
        app->panning = TRUE;
        app->last_pan_x = event->x;
        app->last_pan_y = event->y;
        return TRUE;
    }

    if (event->button == GDK_BUTTON_PRIMARY) {
        app->drawing = TRUE;
        
        // Transform screen coords to world coords
        double wx = (event->x - app->offset_x) / app->scale;
        double wy = (event->y - app->offset_y) / app->scale;

        // Handle Pressure
        double pressure = 1.0;
        GdkDevice *device = gdk_event_get_device((GdkEvent*)event);
        if (device) {
             double p;
             if (gdk_event_get_axis((GdkEvent*)event, GDK_AXIS_PRESSURE, &p)) {
                 pressure = p;
             }
        }
        
        Point p = {wx, wy, pressure};

        // Save history before we start drawing
        save_history(app);
        
        if (app->current_tool == TOOL_PEN || app->current_tool == TOOL_ERASER) {
            // Start smoothing buffer
            app->point_count = 1;
            app->points[0] = p;
            
            // Draw a dot for the initial press
            cairo_t *cr = cairo_create(app->surface);
            if (app->current_tool == TOOL_PEN) {
                cairo_set_source_rgba(cr, app->current_color.r, app->current_color.g, app->current_color.b, app->current_color.a);
                // Adjust size by pressure slightly
                cairo_set_line_width(cr, app->brush_size * pressure);
            } else {
                cairo_set_source_rgba(cr, app->background_color.r, app->background_color.g, app->background_color.b, app->background_color.a);
                cairo_set_line_width(cr, app->eraser_size);
            }
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            
            cairo_move_to(cr, wx, wy);
            cairo_line_to(cr, wx, wy);
            cairo_stroke(cr);
            cairo_destroy(cr);
            gtk_widget_queue_draw(widget);
        } else if (app->current_tool == TOOL_TEXT) {
            GtkWidget *dialog = gtk_dialog_new_with_buttons("Enter Text", GTK_WINDOW(app->window),
                                                            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                            "_OK", GTK_RESPONSE_OK,
                                                            "_Cancel", GTK_RESPONSE_CANCEL, NULL);
            GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
            GtkWidget *entry = gtk_entry_new();
            gtk_container_add(GTK_CONTAINER(content_area), entry);
            gtk_widget_show_all(dialog);

            if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
                const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
                if (text && strlen(text) > 0) {
                    cairo_t *cr = cairo_create(app->surface);
                    cairo_set_source_rgba(cr, app->current_color.r, app->current_color.g, app->current_color.b, app->current_color.a);
                    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                    cairo_set_font_size(cr, app->brush_size * 5.0); // Font size relative to brush size
                    cairo_move_to(cr, wx, wy);
                    cairo_show_text(cr, text);
                    cairo_destroy(cr);
                    gtk_widget_queue_draw(widget);
                }
            }
            gtk_widget_destroy(dialog);
            app->drawing = FALSE; // Don't start a drag for text
        } else {
            // Shape tools
            app->start_point = p;
            clear_temp_surface(app);
        }
    }
    return TRUE;
}

static void draw_arrow(cairo_t *cr, double x1, double y1, double x2, double y2) {
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);

    double angle = atan2(y2 - y1, x2 - x1);
    double arrow_len = 15;
    double arrow_angle = M_PI / 6;

    cairo_move_to(cr, x2, y2);
    cairo_line_to(cr, x2 - arrow_len * cos(angle - arrow_angle), y2 - arrow_len * sin(angle - arrow_angle));
    cairo_move_to(cr, x2, y2);
    cairo_line_to(cr, x2 - arrow_len * cos(angle + arrow_angle), y2 - arrow_len * sin(angle + arrow_angle));
    cairo_stroke(cr);
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    
    if (app->panning) {
        app->offset_x += (event->x - app->last_pan_x);
        app->offset_y += (event->y - app->last_pan_y);
        app->last_pan_x = event->x;
        app->last_pan_y = event->y;
        gtk_widget_queue_draw(widget);
        return TRUE;
    }

    if (app->drawing && app->surface) {
        // Transform to world coords
        double wx = (event->x - app->offset_x) / app->scale;
        double wy = (event->y - app->offset_y) / app->scale;

        // Dynamic expansion
        int sw = cairo_image_surface_get_width(app->layers[0]);
        int sh = cairo_image_surface_get_height(app->layers[0]);
        if (wx < 50 || wy < 50 || wx > sw - 50 || wy > sh - 50) {
            int new_w = sw, new_h = sh;
            double dx = 0, dy = 0;
            if (wx < 50) { new_w += 1000; dx = 1000; }
            if (wy < 50) { new_h += 1000; dy = 1000; }
            if (wx > sw - 50) new_w += 1000;
            if (wy > sh - 50) new_h += 1000;

            ensure_surface(app, new_w, new_h, dx, dy);
            
            if (dx > 0 || dy > 0) {
                 // Adjust internal points and start_point
                 app->start_point.x += dx; app->start_point.y += dy;
                 for (int i=0; i<app->point_count; i++) { app->points[i].x += dx; app->points[i].y += dy; }
                 
                 // Adjust view offset so it doesn't jump
                 app->offset_x -= dx * app->scale;
                 app->offset_y -= dy * app->scale;
                 
                 // Re-transform wx, wy
                 wx = (event->x - app->offset_x) / app->scale;
                 wy = (event->y - app->offset_y) / app->scale;
            }
        }

        double pressure = 1.0;
        gdk_event_request_motions(event);
        double p_val;
        if (gdk_event_get_axis((GdkEvent*)event, GDK_AXIS_PRESSURE, &p_val)) {
            pressure = p_val;
        }

        Point curr = {wx, wy, pressure};

        if (app->current_tool == TOOL_PEN || app->current_tool == TOOL_ERASER) {
            // Add to buffer
            if (app->point_count < 4) {
                app->points[app->point_count++] = curr;
            } else {
                // Shift
                app->points[0] = app->points[1];
                app->points[1] = app->points[2];
                app->points[2] = app->points[3];
                app->points[3] = curr;
            }

            // If we have enough points, draw segment
            if (app->point_count >= 3) {
                cairo_t *cr = cairo_create(app->surface);
                
                if (app->current_tool == TOOL_PEN) {
                    cairo_set_source_rgba(cr, app->current_color.r, app->current_color.g, app->current_color.b, app->current_color.a);
                    // Use average pressure of segment for width
                    double avg_p = (app->points[1].pressure + app->points[2].pressure) / 2.0;
                    // Clamp pressure effect to be subtle if needed, or full range. 
                    // Let's go full range but ensure min width.
                    double width = app->brush_size * avg_p;
                    if (width < 1.0) width = 1.0;
                    cairo_set_line_width(cr, width);
                } else {
                    cairo_set_source_rgba(cr, app->background_color.r, app->background_color.g, app->background_color.b, app->background_color.a);
                    cairo_set_line_width(cr, app->eraser_size);
                }
                
                cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
                
                draw_smooth_segment(app, cr);
                
                cairo_destroy(cr);
                gtk_widget_queue_draw(widget);
            }
        } 
        else {
            // Preview Shapes
            clear_temp_surface(app);
            cairo_t *cr = cairo_create(app->temp_surface);
            cairo_set_source_rgba(cr, app->current_color.r, app->current_color.g, app->current_color.b, app->current_color.a);
            cairo_set_line_width(cr, app->brush_size);
            
            if (app->current_tool == TOOL_LINE) {
                cairo_move_to(cr, app->start_point.x, app->start_point.y);
                cairo_line_to(cr, wx, wy);
                cairo_stroke(cr);
            } else if (app->current_tool == TOOL_RECTANGLE) {
                cairo_rectangle(cr, app->start_point.x, app->start_point.y, wx - app->start_point.x, wy - app->start_point.y);
                cairo_stroke(cr);
            } else if (app->current_tool == TOOL_CIRCLE) {
                double dx = wx - app->start_point.x;
                double dy = wy - app->start_point.y;
                double r = sqrt(dx*dx + dy*dy);
                cairo_arc(cr, app->start_point.x, app->start_point.y, r, 0, 2 * M_PI);
                cairo_stroke(cr);
            } else if (app->current_tool == TOOL_ARROW) {
                draw_arrow(cr, app->start_point.x, app->start_point.y, wx, wy);
            }
            cairo_destroy(cr);
            gtk_widget_queue_draw(widget);
        }
    }
    return TRUE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    
    if (event->button == GDK_BUTTON_MIDDLE) {
        app->panning = FALSE;
        return TRUE;
    }

    if (event->button == GDK_BUTTON_PRIMARY && app->drawing) {
        app->drawing = FALSE;

        double wx = (event->x - app->offset_x) / app->scale;
        double wy = (event->y - app->offset_y) / app->scale;

        if (app->current_tool == TOOL_PEN || app->current_tool == TOOL_ERASER) {
             // Finish the line if there are remaining points
             cairo_t *cr = cairo_create(app->surface);
             if (app->current_tool == TOOL_PEN) {
                cairo_set_source_rgba(cr, app->current_color.r, app->current_color.g, app->current_color.b, app->current_color.a);
                cairo_set_line_width(cr, app->brush_size * (app->point_count > 0 ? app->points[app->point_count-1].pressure : 1.0));
             } else {
                cairo_set_source_rgba(cr, app->background_color.r, app->background_color.g, app->background_color.b, app->background_color.a);
                cairo_set_line_width(cr, app->eraser_size);
             }
             cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
             cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
             
             // Draw remaining
             if (app->point_count >= 2) {
                 Point p_last = app->points[app->point_count-1];
                 Point p_prev = app->points[app->point_count-2];
                 double mid_x = (p_prev.x + p_last.x) / 2.0;
                 double mid_y = (p_prev.y + p_last.y) / 2.0;
                 
                 cairo_move_to(cr, mid_x, mid_y);
                 cairo_line_to(cr, wx, wy);
                 cairo_stroke(cr);
             } else if (app->point_count == 1) {
                 cairo_move_to(cr, app->points[0].x, app->points[0].y);
                 cairo_line_to(cr, wx, wy);
                 cairo_stroke(cr);
             }
             cairo_destroy(cr);
        } else {
            // Commit Shape
            clear_temp_surface(app);
            cairo_t *cr = cairo_create(app->surface);
            cairo_set_source_rgba(cr, app->current_color.r, app->current_color.g, app->current_color.b, app->current_color.a);
            cairo_set_line_width(cr, app->brush_size);

            if (app->current_tool == TOOL_LINE) {
                cairo_move_to(cr, app->start_point.x, app->start_point.y);
                cairo_line_to(cr, wx, wy);
                cairo_stroke(cr);
            } else if (app->current_tool == TOOL_RECTANGLE) {
                cairo_rectangle(cr, app->start_point.x, app->start_point.y, wx - app->start_point.x, wy - app->start_point.y);
                cairo_stroke(cr);
            } else if (app->current_tool == TOOL_CIRCLE) {
                double dx = wx - app->start_point.x;
                double dy = wy - app->start_point.y;
                double r = sqrt(dx*dx + dy*dy);
                cairo_arc(cr, app->start_point.x, app->start_point.y, r, 0, 2 * M_PI);
                cairo_stroke(cr);
            } else if (app->current_tool == TOOL_ARROW) {
                draw_arrow(cr, app->start_point.x, app->start_point.y, wx, wy);
            }
            cairo_destroy(cr);
        }
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

// --- UI Callbacks ---

static void on_tool_toggled(GtkToggleButton *btn, gpointer user_data) {
    (void)user_data;
    if (gtk_toggle_button_get_active(btn)) {
        // Find which tool this is
        // We stored the tool index in the widget data or just deduce it?
        // Let's pass the index via pointer to int or something. 
        // Simpler: check equality of widget pointer
        AppState *app = (AppState *)g_object_get_data(G_OBJECT(btn), "app_ptr");
        ToolType tool = (ToolType)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "tool_id"));
        
        app->current_tool = tool;
        update_tool_buttons(app); // Ensure others are untoggled if manually clicked (though radio behavior works too)
    }
}

static void on_page_combo_changed(GtkComboBox *widget, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    app->current_page_type = (PageType)gtk_combo_box_get_active(widget);
    gtk_widget_queue_draw(app->drawing_area);
}

static void on_layer_combo_changed(GtkComboBox *widget, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    int layer_idx = gtk_combo_box_get_active(widget);
    app->current_layer = layer_idx;
    app->surface = app->layers[layer_idx];
}

static void on_brush_size_changed(GtkRange *range, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    app->brush_size = gtk_range_get_value(range);
}

static void on_eraser_size_changed(GtkRange *range, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    app->eraser_size = gtk_range_get_value(range);
}

static void on_color_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    // Get color from button's data or style?
    // We'll store the color in the button data
    Color *col = (Color *)g_object_get_data(G_OBJECT(btn), "color_val");
    if (col) {
        app->current_color = *col;
    }
}

static void on_custom_color_clicked(GtkColorButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    GdkRGBA c;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &c);
    app->current_color = make_color(c.red, c.green, c.blue, c.alpha);
}

static void on_background_color_clicked(GtkColorButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    GdkRGBA c;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &c);
    app->background_color = make_color(c.red, c.green, c.blue, c.alpha);
    
    // Redraw background
    gtk_widget_queue_draw(app->drawing_area);
}

static void export_canvas(AppState *app, const char *filename) {
    if (!app->layers[0]) return;
    
    int w = cairo_image_surface_get_width(app->layers[0]);
    int h = cairo_image_surface_get_height(app->layers[0]);
    
    cairo_surface_t *export_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(export_surf);
    
    // Background color (solid for export)
    cairo_set_source_rgba(cr, app->background_color.r, app->background_color.g, app->background_color.b, app->background_color.a);
    cairo_paint(cr);
    
    // Layers
    for (int i = 0; i < 2; i++) {
        cairo_set_source_surface(cr, app->layers[i], 0, 0);
        cairo_paint(cr);
    }
    
    cairo_destroy(cr);
    cairo_surface_write_to_png(export_surf, filename);
    cairo_surface_destroy(export_surf);
}

static void on_save_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)btn;

    GtkWidget *dialog;
    GtkFileChooser *chooser;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;
    gint res;

    dialog = gtk_file_chooser_dialog_new("Save Canvas",
                                         GTK_WINDOW(app->window),
                                         action,
                                         "_Cancel",
                                         GTK_RESPONSE_CANCEL,
                                         "_Save",
                                         GTK_RESPONSE_ACCEPT,
                                         NULL);
    chooser = GTK_FILE_CHOOSER(dialog);

    gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    gtk_file_chooser_set_current_name(chooser, "drawing.png");

    res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        char *filename;
        filename = gtk_file_chooser_get_filename(chooser);
        
        export_canvas(app, filename);
        
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

static void on_clear_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)btn;
    save_history(app);
    clear_surface(app->surface, app->background_color);
    gtk_widget_queue_draw(app->drawing_area);
}

// --- UI Construction ---

static GtkWidget* create_color_button(AppState *app, double r, double g, double b, double a) {
    GtkWidget *btn = gtk_button_new();
    gtk_widget_set_size_request(btn, 25, 25);
    
    // Store color
    Color *col = malloc(sizeof(Color)); // Small leak, strictly speaking should free on destroy
    *col = make_color(r, g, b, a);
    g_object_set_data_full(G_OBJECT(btn), "color_val", col, free);
    
    // CSS for background
    GtkCssProvider *provider = gtk_css_provider_new();
    char css[256];
    // Use '*' to ensure it applies to the button itself, and background-image: none to override themes
    snprintf(css, sizeof(css), "button { background-color: rgba(%d, %d, %d, %.2f); background-image: none; }", 
             (int)(r*255), (int)(g*255), (int)(b*255), a);
    
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context(btn);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    g_signal_connect(btn, "clicked", G_CALLBACK(on_color_clicked), app);
    return btn;
}

static GtkWidget* create_sidebar(AppState *app) {
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, 220, -1);

    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    g_object_set(sidebar, "margin", 10, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), sidebar);

    // Tools
    GtkWidget *tools_frame = gtk_frame_new("Tools");
    GtkWidget *tools_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(tools_grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(tools_grid), 2);
    g_object_set(tools_grid, "margin", 5, NULL);
    
    const char *tool_names[] = {"Pen", "Eraser", "Line", "Rect", "Circ", "Arrow", "Text"};
    for (int i = 0; i < 7; i++) {
        GtkWidget *btn = gtk_toggle_button_new_with_label(tool_names[i]);
        g_object_set_data(G_OBJECT(btn), "app_ptr", app);
        g_object_set_data(G_OBJECT(btn), "tool_id", GINT_TO_POINTER(i));
        g_signal_connect(btn, "toggled", G_CALLBACK(on_tool_toggled), NULL);
        gtk_grid_attach(GTK_GRID(tools_grid), btn, i % 2, i / 2, 1, 1);
        app->tool_buttons[i] = btn;
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->tool_buttons[0]), TRUE);
    
    gtk_container_add(GTK_CONTAINER(tools_frame), tools_grid);
    gtk_box_pack_start(GTK_BOX(sidebar), tools_frame, FALSE, FALSE, 0);

    // Page Style & Layers (Combined for space)
    GtkWidget *style_frame = gtk_frame_new("Page & Layers");
    GtkWidget *style_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    g_object_set(style_box, "margin", 5, NULL);

    GtkWidget *page_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(page_combo), "Plain Page");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(page_combo), "Grid Page");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(page_combo), "Lined Page");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(page_combo), "Dotted Page");
    gtk_combo_box_set_active(GTK_COMBO_BOX(page_combo), 0);
    g_signal_connect(page_combo, "changed", G_CALLBACK(on_page_combo_changed), app);
    gtk_box_pack_start(GTK_BOX(style_box), page_combo, FALSE, FALSE, 0);

    GtkWidget *layer_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(layer_combo), "Background Layer");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(layer_combo), "Foreground Layer");
    gtk_combo_box_set_active(GTK_COMBO_BOX(layer_combo), 1);
    g_signal_connect(layer_combo, "changed", G_CALLBACK(on_layer_combo_changed), app);
    gtk_box_pack_start(GTK_BOX(style_box), layer_combo, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(style_frame), style_box);
    gtk_box_pack_start(GTK_BOX(sidebar), style_frame, FALSE, FALSE, 0);

    // Brush Size
    GtkWidget *brush_frame = gtk_frame_new("Sizes");
    GtkWidget *brush_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    g_object_set(brush_box, "margin", 5, NULL);
    
    gtk_box_pack_start(GTK_BOX(brush_box), gtk_label_new("Brush"), FALSE, FALSE, 0);
    app->brush_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 50, 1);
    gtk_range_set_value(GTK_RANGE(app->brush_scale), app->brush_size);
    g_signal_connect(app->brush_scale, "value-changed", G_CALLBACK(on_brush_size_changed), app);
    gtk_box_pack_start(GTK_BOX(brush_box), app->brush_scale, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(brush_box), gtk_label_new("Eraser"), FALSE, FALSE, 0);
    app->eraser_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 5, 100, 5);
    gtk_range_set_value(GTK_RANGE(app->eraser_scale), app->eraser_size);
    g_signal_connect(app->eraser_scale, "value-changed", G_CALLBACK(on_eraser_size_changed), app);
    gtk_box_pack_start(GTK_BOX(brush_box), app->eraser_scale, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(brush_frame), brush_box);
    gtk_box_pack_start(GTK_BOX(sidebar), brush_frame, FALSE, FALSE, 0);

    // Colors
    GtkWidget *colors_frame = gtk_frame_new("Colors");
    GtkWidget *colors_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    g_object_set(colors_box, "margin", 5, NULL);
    
    // Custom Pickers Row
    GtkWidget *custom_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(custom_grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(custom_grid), 10);

    // Pen Color
    gtk_grid_attach(GTK_GRID(custom_grid), gtk_label_new("Pen"), 0, 0, 1, 1);
    GtkWidget *pen_color_btn = gtk_color_button_new_with_rgba(&(GdkRGBA){0,0,0,1});
    g_signal_connect(pen_color_btn, "color-set", G_CALLBACK(on_custom_color_clicked), app);
    gtk_grid_attach(GTK_GRID(custom_grid), pen_color_btn, 1, 0, 1, 1);

    // Quick Select for Pen (A small row of common colors)
    GtkWidget *quick_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
    double quick_colors[][4] = {{0,0,0,1}, {1,0,0,1}, {0,0.7,0,1}, {0,0,1,1}, {1,1,0,1}};
    for (int i=0; i<5; i++) {
        GtkWidget *q_btn = create_color_button(app, quick_colors[i][0], quick_colors[i][1], quick_colors[i][2], quick_colors[i][3]);
        gtk_widget_set_size_request(q_btn, 20, 20);
        gtk_box_pack_start(GTK_BOX(quick_box), q_btn, FALSE, FALSE, 0);
    }
    gtk_grid_attach(GTK_GRID(custom_grid), quick_box, 2, 0, 1, 1);

    // BG Color
    gtk_grid_attach(GTK_GRID(custom_grid), gtk_label_new("BG"), 0, 1, 1, 1);
    GtkWidget *bg_color_btn = gtk_color_button_new_with_rgba(&(GdkRGBA){1,1,1,1});
    g_signal_connect(bg_color_btn, "color-set", G_CALLBACK(on_background_color_clicked), app);
    gtk_grid_attach(GTK_GRID(custom_grid), bg_color_btn, 1, 1, 1, 1);

    gtk_box_pack_start(GTK_BOX(colors_box), custom_grid, FALSE, FALSE, 0);
    
    gtk_container_add(GTK_CONTAINER(colors_frame), colors_box);
    gtk_box_pack_start(GTK_BOX(sidebar), colors_frame, FALSE, FALSE, 0);

    // Actions
    GtkWidget *act_frame = gtk_frame_new("Actions");
    GtkWidget *act_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    g_object_set(act_box, "margin", 5, NULL);
    
    GtkWidget *undo_redo_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget *undo_btn = gtk_button_new_with_label("Undo");
    g_signal_connect(undo_btn, "clicked", G_CALLBACK(on_undo_clicked), app);
    gtk_box_pack_start(GTK_BOX(undo_redo_box), undo_btn, TRUE, TRUE, 0);

    GtkWidget *redo_btn = gtk_button_new_with_label("Redo");
    g_signal_connect(redo_btn, "clicked", G_CALLBACK(on_redo_clicked), app);
    gtk_box_pack_start(GTK_BOX(undo_redo_box), redo_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(act_box), undo_redo_box, FALSE, FALSE, 0);

    GtkWidget *save_btn = gtk_button_new_with_label("💾 Save");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_clicked), app);
    gtk_box_pack_start(GTK_BOX(act_box), save_btn, FALSE, FALSE, 0);

    GtkWidget *clr_btn = gtk_button_new_with_label("🗑️ Clear");
    g_signal_connect(clr_btn, "clicked", G_CALLBACK(on_clear_clicked), app);
    gtk_box_pack_start(GTK_BOX(act_box), clr_btn, FALSE, FALSE, 0);
    
    gtk_container_add(GTK_CONTAINER(act_frame), act_box);
    gtk_box_pack_start(GTK_BOX(sidebar), act_frame, FALSE, FALSE, 0);

    return scrolled;
}

// --- Main ---

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    AppState *app = malloc(sizeof(AppState));
    // Defaults
    app->current_tool = TOOL_PEN;
    app->current_page_type = PAGE_PLAIN;
    app->current_color = make_color(0, 0, 0, 1);
    app->background_color = make_color(1, 1, 1, 1);
    app->brush_size = 3.0;
    app->eraser_size = 10.0;
    app->offset_x = 0.0;
    app->offset_y = 0.0;
    app->scale = 1.0;
    app->panning = FALSE;
    app->layers[0] = NULL;
    app->layers[1] = NULL;
    app->current_layer = 1;
    app->surface = NULL;
    app->temp_surface = NULL;
    app->drawing = FALSE;
    app->point_count = 0;
    app->history_index = -1;
    app->history_max = -1;
    for (int i = 0; i < MAX_UNDO; i++) app->undo_stack[i] = NULL;

    // Window
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "Splashy - Advanced Whiteboard (C)");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1000, 700);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Layout
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(app->window), hbox);

    // Drawing Area
    app->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_events(app->drawing_area, 
                          GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | 
                          GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK);
    
    g_signal_connect(app->drawing_area, "draw", G_CALLBACK(on_draw), app);
    g_signal_connect(app->drawing_area, "configure-event", G_CALLBACK(on_configure), app);
    g_signal_connect(app->drawing_area, "button-press-event", G_CALLBACK(on_button_press), app);
    g_signal_connect(app->drawing_area, "button-release-event", G_CALLBACK(on_button_release), app);
    g_signal_connect(app->drawing_area, "motion-notify-event", G_CALLBACK(on_motion_notify), app);
    g_signal_connect(app->drawing_area, "scroll-event", G_CALLBACK(on_scroll), app);

    // Sidebar
    GtkWidget *sidebar = create_sidebar(app);
    gtk_box_pack_start(GTK_BOX(hbox), sidebar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), app->drawing_area, TRUE, TRUE, 0);

    gtk_widget_show_all(app->window);
    
    gtk_main();
    
    // Cleanup if we exit loop
    if (app->surface) cairo_surface_destroy(app->surface);
    if (app->temp_surface) cairo_surface_destroy(app->temp_surface);
    free(app);

    return 0;
}
