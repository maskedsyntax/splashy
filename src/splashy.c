#include <gtk/gtk.h>
#include <cairo.h>
#include <cairo-pdf.h>
#include <math.h>
#include <stdlib.h>
#include <pango/pangocairo.h>

// --- Constants & Enums ---

typedef enum {
    TOOL_PEN,
    TOOL_ERASER,
    TOOL_HIGHLIGHTER,
    TOOL_BUCKET,
    TOOL_SELECT,
    TOOL_LINE,
    TOOL_RECTANGLE,
    TOOL_CIRCLE,
    TOOL_TRIANGLE,
    TOOL_STAR,
    TOOL_ARROW,
    TOOL_TEXT,
    TOOL_COUNT
} ToolType;

// --- Data Structures ---

#define MAX_UNDO 100

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
    cairo_surface_t *surface;
    char *name;
    gboolean visible;
    double alpha;
} Layer;

typedef struct {
    GtkWidget *window;
    GtkWidget *drawing_area;
    
    // UI References
    GtkWidget *brush_scale;
    GtkWidget *eraser_scale;
    GtkWidget *tool_buttons[TOOL_COUNT]; // Indexed by ToolType
    GtkComboBoxText *layer_combo;

    // Layers
    GList *layer_list;
    Layer *active_layer;
    cairo_surface_t *surface;      // Points to active_layer->surface
    cairo_surface_t *temp_surface; // Preview surface for shapes

    // Selection State
    cairo_surface_t *selection_surf;
    double sel_x, sel_y, sel_w, sel_h;
    gboolean has_selection;
    gboolean dragging_selection;
    double sel_drag_offset_x, sel_drag_offset_y;

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
    char *font_name;
    gboolean snap_to_grid;
    gboolean dark_mode;
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

// --- File Format Structures ---

#define PROJECT_MAGIC "SPLASHY"
#define PROJECT_VERSION 1

typedef struct {
    char magic[8];
    int version;
    int width;
    int height;
    int layer_count;
    int active_layer_index;
    double bg_r, bg_g, bg_b, bg_a;
    int page_type;
    double offset_x;
    double offset_y;
    double scale;
} ProjectHeader;

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
    size_t read_pos;
} MemBuffer;

static cairo_status_t write_to_buffer(void *closure, const unsigned char *data, unsigned int length) {
    MemBuffer *buf = (MemBuffer *)closure;
    if (buf->size + length > buf->capacity) {
        buf->capacity = (buf->size + length) * 2 + 1024;
        buf->data = realloc(buf->data, buf->capacity);
    }
    memcpy(buf->data + buf->size, data, length);
    buf->size += length;
    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t read_from_buffer(void *closure, unsigned char *data, unsigned int length) {
    MemBuffer *buf = (MemBuffer *)closure;
    if (buf->read_pos + length > buf->size) return CAIRO_STATUS_READ_ERROR;
    memcpy(data, buf->data + buf->read_pos, length);
    buf->read_pos += length;
    return CAIRO_STATUS_SUCCESS;
}

// --- Forward Declarations ---

static void clear_surface(cairo_surface_t *surface, Color col);
static void clear_temp_surface(AppState *app);
static void save_history(AppState *app);
static void on_save_clicked(GtkButton *btn, gpointer user_data);
static void on_save_project_clicked(GtkButton *btn, gpointer user_data);
static void on_open_clicked(GtkButton *btn, gpointer user_data);

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

static void apply_snap(AppState *app, double *x, double *y) {
    if (app->snap_to_grid && (app->current_page_type == PAGE_GRID || app->current_page_type == PAGE_DOTTED)) {
        double step = 30.0;
        *x = round(*x / step) * step;
        *y = round(*y / step) * step;
    }
}

static void update_tool_buttons(AppState *app) {
    for (int i = 0; i < TOOL_COUNT; i++) {
        if ((ToolType)i == app->current_tool) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->tool_buttons[i]), TRUE);
        } else {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->tool_buttons[i]), FALSE);
        }
    }
}

// --- Flood Fill Algorithm ---

typedef struct {
    int x, y;
} IntPoint;

static void flood_fill(cairo_surface_t *surface, int start_x, int start_y, Color fill_color) {
    if (cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32) return;

    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);
    unsigned char *data = cairo_image_surface_get_data(surface);

    cairo_surface_flush(surface);

    if (start_x < 0 || start_x >= width || start_y < 0 || start_y >= height) return;

    uint32_t *pixels = (uint32_t *)data;
    int p_stride = stride / 4;
    uint32_t target_pixel = pixels[start_y * p_stride + start_x];

    // Convert Color to uint32_t (premultiplied ARGB32)
    unsigned char a = (unsigned char)(fill_color.a * 255);
    unsigned char r = (unsigned char)(fill_color.r * fill_color.a * 255);
    unsigned char g = (unsigned char)(fill_color.g * fill_color.a * 255);
    unsigned char b = (unsigned char)(fill_color.b * fill_color.a * 255);
    uint32_t fill_pixel = (a << 24) | (r << 16) | (g << 8) | b;

    if (target_pixel == fill_pixel) return;

    IntPoint *queue = malloc(sizeof(IntPoint) * width * height);
    if (!queue) return;
    int head = 0, tail = 0;

    queue[tail++] = (IntPoint){start_x, start_y};
    pixels[start_y * p_stride + start_x] = fill_pixel;

    while (head < tail) {
        IntPoint p = queue[head++];
        
        static const int dx[] = {1, -1, 0, 0};
        static const int dy[] = {0, 0, 1, -1};
        
        for (int i = 0; i < 4; i++) {
            int nx = p.x + dx[i];
            int ny = p.y + dy[i];
            
            if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                if (pixels[ny * p_stride + nx] == target_pixel) {
                    pixels[ny * p_stride + nx] = fill_pixel;
                    queue[tail++] = (IntPoint){nx, ny};
                }
            }
        }
    }
    free(queue);
    cairo_surface_mark_dirty(surface);
}

// --- Drawing Logic ---

static void ensure_surface(AppState *app, int width, int height, double dx, double dy) {
    if (!app->layer_list) {
        // Create initial layer
        Layer *l = malloc(sizeof(Layer));
        l->name = g_strdup("Layer 1");
        l->visible = TRUE;
        l->alpha = 1.0;
        l->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        clear_surface(l->surface, app->background_color);
        app->layer_list = g_list_append(NULL, l);
        app->active_layer = l;
        app->surface = l->surface;
        
        app->temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        clear_temp_surface(app);

        save_history(app);
        return;
    }

    Layer *first_layer = (Layer *)app->layer_list->data;
    int old_w = cairo_image_surface_get_width(first_layer->surface);
    int old_h = cairo_image_surface_get_height(first_layer->surface);

    if (width > old_w || height > old_h || dx > 0 || dy > 0) {
        int new_w = (width > old_w) ? width : old_w;
        int new_h = (height > old_h) ? height : old_h;

        for (GList *l = app->layer_list; l != NULL; l = l->next) {
            Layer *layer = (Layer *)l->data;
            cairo_surface_t *new_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_w, new_h);
            clear_surface(new_surf, app->background_color);

            cairo_t *cr = cairo_create(new_surf);
            cairo_set_source_surface(cr, layer->surface, dx, dy);
            cairo_paint(cr);
            cairo_destroy(cr);

            cairo_surface_destroy(layer->surface);
            layer->surface = new_surf;
        }
        app->surface = app->active_layer->surface;

        cairo_surface_destroy(app->temp_surface);
        app->temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_w, new_h);
        clear_temp_surface(app);
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

    for (GList *l = app->layer_list; l != NULL; l = l->next) {
        Layer *layer = (Layer *)l->data;
        if (layer->visible && layer->surface) {
            cairo_set_source_surface(cr, layer->surface, 0, 0);
            cairo_paint_with_alpha(cr, layer->alpha);
        }
    }
    
    if (app->has_selection && app->selection_surf) {
        cairo_set_source_surface(cr, app->selection_surf, app->sel_x, app->sel_y);
        cairo_paint(cr);
        
        cairo_set_source_rgba(cr, 0, 0, 1, 0.8);
        cairo_set_line_width(cr, 1.0 / app->scale);
        cairo_rectangle(cr, app->sel_x, app->sel_y, app->sel_w, app->sel_h);
        cairo_stroke(cr);
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

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)widget;

    if ((event->state & GDK_CONTROL_MASK)) {
        switch (event->keyval) {
            case GDK_KEY_z:
                if (event->state & GDK_SHIFT_MASK) redo(app);
                else undo(app);
                return TRUE;
            case GDK_KEY_y:
                redo(app);
                return TRUE;
            case GDK_KEY_s:
                on_save_project_clicked(NULL, app);
                return TRUE;
            case GDK_KEY_e:
                on_save_clicked(NULL, app); // Export
                return TRUE;
            case GDK_KEY_o:
                on_open_clicked(NULL, app);
                return TRUE;
        }
    }
    return FALSE;
}

static gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    
    // Check for Control key to Zoom, otherwise Pan
    if (event->state & GDK_CONTROL_MASK) {
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
        app->offset_x = event->x - (event->x - app->offset_x) * zoom_factor;
        app->offset_y = event->y - (event->y - app->offset_y) * zoom_factor;
    } else {
        // Pan
        double delta_x = 0, delta_y = 0;
        double scroll_step = 30.0;
        
        if (event->direction == GDK_SCROLL_UP) delta_y = scroll_step;
        else if (event->direction == GDK_SCROLL_DOWN) delta_y = -scroll_step;
        else if (event->direction == GDK_SCROLL_LEFT) delta_x = scroll_step;
        else if (event->direction == GDK_SCROLL_RIGHT) delta_x = -scroll_step;
        else if (event->direction == GDK_SCROLL_SMOOTH) {
            gdk_event_get_scroll_deltas((GdkEvent*)event, &delta_x, &delta_y);
            delta_x *= -scroll_step; // Invert to feel natural
            delta_y *= -scroll_step;
        }
        
        app->offset_x += delta_x;
        app->offset_y += delta_y;
    }

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

        if (app->current_tool != TOOL_PEN && app->current_tool != TOOL_ERASER && app->current_tool != TOOL_HIGHLIGHTER) {
            apply_snap(app, &wx, &wy);
        }

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
        
        if (app->current_tool == TOOL_SELECT) {
            if (app->has_selection && 
                wx >= app->sel_x && wx <= app->sel_x + app->sel_w &&
                wy >= app->sel_y && wy <= app->sel_y + app->sel_h) {
                app->dragging_selection = TRUE;
                app->sel_drag_offset_x = wx - app->sel_x;
                app->sel_drag_offset_y = wy - app->sel_y;
            } else {
                if (app->has_selection) {
                    cairo_t *cr = cairo_create(app->surface);
                    cairo_set_source_surface(cr, app->selection_surf, app->sel_x, app->sel_y);
                    cairo_paint(cr);
                    cairo_destroy(cr);
                    app->has_selection = FALSE;
                    cairo_surface_destroy(app->selection_surf);
                    app->selection_surf = NULL;
                }
                app->start_point = p;
                app->drawing = TRUE;
            }
            return TRUE;
        }

        if (app->current_tool == TOOL_BUCKET) {
            flood_fill(app->surface, (int)wx, (int)wy, app->current_color);
            gtk_widget_queue_draw(widget);
            app->drawing = FALSE;
            return TRUE;
        }

        if (app->current_tool == TOOL_PEN || app->current_tool == TOOL_ERASER || app->current_tool == TOOL_HIGHLIGHTER) {
            // Start smoothing buffer
            app->point_count = 1;
            app->points[0] = p;
            
            // Draw a dot for the initial press
            cairo_t *cr = cairo_create(app->surface);
            if (app->current_tool == TOOL_PEN || app->current_tool == TOOL_HIGHLIGHTER) {
                double a = app->current_color.a;
                double size = app->brush_size;
                if (app->current_tool == TOOL_HIGHLIGHTER) {
                    a *= 0.35; 
                    size *= 4.0;
                } else {
                    size *= pressure;
                }
                cairo_set_source_rgba(cr, app->current_color.r, app->current_color.g, app->current_color.b, a);
                cairo_set_line_width(cr, size);
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
                    
                    PangoLayout *layout = pango_cairo_create_layout(cr);
                    PangoFontDescription *desc = pango_font_description_from_string(app->font_name);
                    pango_layout_set_font_description(layout, desc);
                    pango_layout_set_text(layout, text, -1);
                    
                    cairo_move_to(cr, wx, wy);
                    pango_cairo_show_layout(cr, layout);
                    
                    pango_font_description_free(desc);
                    g_object_unref(layout);
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

static void draw_triangle(cairo_t *cr, double x1, double y1, double x2, double y2) {
    double mx = (x1 + x2) / 2.0;
    cairo_move_to(cr, mx, y1);
    cairo_line_to(cr, x1, y2);
    cairo_line_to(cr, x2, y2);
    cairo_close_path(cr);
    cairo_stroke(cr);
}

static void draw_star(cairo_t *cr, double x1, double y1, double x2, double y2) {
    double cx = (x1 + x2) / 2.0;
    double cy = (y1 + y2) / 2.0;
    double dx = x2 - cx;
    double dy = y2 - cy;
    double r_outer = sqrt(dx*dx + dy*dy);
    double r_inner = r_outer * 0.4;
    int points = 5;
    double angle_step = M_PI / points;
    
    for (int i = 0; i < 2 * points; i++) {
        double r = (i % 2 == 0) ? r_outer : r_inner;
        double a = i * angle_step - M_PI / 2.0;
        double px = cx + r * cos(a);
        double py = cy + r * sin(a);
        if (i == 0) cairo_move_to(cr, px, py);
        else cairo_line_to(cr, px, py);
    }
    cairo_close_path(cr);
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

        if (app->current_tool != TOOL_PEN && app->current_tool != TOOL_ERASER && app->current_tool != TOOL_HIGHLIGHTER) {
            apply_snap(app, &wx, &wy);
        }

        if (app->current_tool == TOOL_SELECT) {
            if (app->dragging_selection) {
                app->sel_x = wx - app->sel_drag_offset_x;
                app->sel_y = wy - app->sel_drag_offset_y;
                gtk_widget_queue_draw(widget);
            } else if (app->drawing) {
                clear_temp_surface(app);
                cairo_t *cr = cairo_create(app->temp_surface);
                cairo_set_source_rgba(cr, 0, 0, 1, 0.5); 
                cairo_set_line_width(cr, 1.0);
                double dashes[] = {4.0, 4.0};
                cairo_set_dash(cr, dashes, 2, 0);
                cairo_rectangle(cr, app->start_point.x, app->start_point.y, wx - app->start_point.x, wy - app->start_point.y);
                cairo_stroke(cr);
                cairo_destroy(cr);
                gtk_widget_queue_draw(widget);
            }
            return TRUE;
        }

        // Dynamic expansion
        Layer *first = (Layer *)app->layer_list->data;
        int sw = cairo_image_surface_get_width(first->surface);
        int sh = cairo_image_surface_get_height(first->surface);
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

        if (app->current_tool == TOOL_PEN || app->current_tool == TOOL_ERASER || app->current_tool == TOOL_HIGHLIGHTER) {
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
                
                if (app->current_tool == TOOL_PEN || app->current_tool == TOOL_HIGHLIGHTER) {
                    double a = app->current_color.a;
                    double width = app->brush_size;
                    
                    if (app->current_tool == TOOL_HIGHLIGHTER) {
                        a *= 0.35;
                        width *= 4.0;
                    } else {
                        double avg_p = (app->points[1].pressure + app->points[2].pressure) / 2.0;
                        width *= avg_p;
                        if (width < 1.0) width = 1.0;
                    }
                    cairo_set_source_rgba(cr, app->current_color.r, app->current_color.g, app->current_color.b, a);
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
            } else if (app->current_tool == TOOL_TRIANGLE) {
                draw_triangle(cr, app->start_point.x, app->start_point.y, wx, wy);
            } else if (app->current_tool == TOOL_STAR) {
                draw_star(cr, app->start_point.x, app->start_point.y, wx, wy);
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
        double wx = (event->x - app->offset_x) / app->scale;
        double wy = (event->y - app->offset_y) / app->scale;

        if (app->current_tool != TOOL_PEN && app->current_tool != TOOL_ERASER && app->current_tool != TOOL_HIGHLIGHTER) {
            apply_snap(app, &wx, &wy);
        }

        if (app->current_tool == TOOL_SELECT) {
            if (app->dragging_selection) {
                app->dragging_selection = FALSE;
            } else if (app->drawing) {
                app->drawing = FALSE;
                clear_temp_surface(app);
                
                double x1 = app->start_point.x;
                double y1 = app->start_point.y;
                double x2 = wx;
                double y2 = wy;
                
                app->sel_x = (x1 < x2) ? x1 : x2;
                app->sel_y = (y1 < y2) ? y1 : y2;
                app->sel_w = fabs(x2 - x1);
                app->sel_h = fabs(y2 - y1);
                
                if (app->sel_w > 1 && app->sel_h > 1) {
                    app->selection_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)app->sel_w, (int)app->sel_h);
                    cairo_t *cr = cairo_create(app->selection_surf);
                    cairo_set_source_surface(cr, app->surface, -app->sel_x, -app->sel_y);
                    cairo_paint(cr);
                    cairo_destroy(cr);
                    
                    cr = cairo_create(app->surface);
                    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
                    cairo_rectangle(cr, app->sel_x, app->sel_y, app->sel_w, app->sel_h);
                    cairo_fill(cr);
                    cairo_destroy(cr);
                    
                    app->has_selection = TRUE;
                }
                gtk_widget_queue_draw(widget);
            }
            return TRUE;
        }

        app->drawing = FALSE;

        if (app->current_tool == TOOL_PEN || app->current_tool == TOOL_ERASER || app->current_tool == TOOL_HIGHLIGHTER) {
             // Finish the line if there are remaining points
             cairo_t *cr = cairo_create(app->surface);
             if (app->current_tool == TOOL_PEN || app->current_tool == TOOL_HIGHLIGHTER) {
                double a = app->current_color.a;
                double size = app->brush_size;
                if (app->current_tool == TOOL_HIGHLIGHTER) {
                    a *= 0.35;
                    size *= 4.0;
                } else {
                    size *= (app->point_count > 0 ? app->points[app->point_count-1].pressure : 1.0);
                }
                cairo_set_source_rgba(cr, app->current_color.r, app->current_color.g, app->current_color.b, a);
                cairo_set_line_width(cr, size);
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
            } else if (app->current_tool == TOOL_TRIANGLE) {
                draw_triangle(cr, app->start_point.x, app->start_point.y, wx, wy);
            } else if (app->current_tool == TOOL_STAR) {
                draw_star(cr, app->start_point.x, app->start_point.y, wx, wy);
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
        AppState *app = (AppState *)g_object_get_data(G_OBJECT(btn), "app_ptr");
        ToolType tool = (ToolType)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "tool_id"));
        
        // If we switched away from select tool, commit selection
        if (app->current_tool == TOOL_SELECT && tool != TOOL_SELECT && app->has_selection) {
             cairo_t *cr = cairo_create(app->surface);
             cairo_set_source_surface(cr, app->selection_surf, app->sel_x, app->sel_y);
             cairo_paint(cr);
             cairo_destroy(cr);
             app->has_selection = FALSE;
             if (app->selection_surf) {
                 cairo_surface_destroy(app->selection_surf);
                 app->selection_surf = NULL;
             }
             gtk_widget_queue_draw(app->drawing_area);
        }

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
    if (layer_idx < 0) return;
    
    Layer *l = (Layer *)g_list_nth_data(app->layer_list, layer_idx);
    if (l) {
        app->active_layer = l;
        app->surface = l->surface;
    }
}

static void on_add_layer_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)btn;
    
    Layer *first = (Layer *)app->layer_list->data;
    int w = cairo_image_surface_get_width(first->surface);
    int h = cairo_image_surface_get_height(first->surface);
    
    Layer *l = malloc(sizeof(Layer));
    l->name = g_strdup_printf("Layer %d", g_list_length(app->layer_list) + 1);
    l->visible = TRUE;
    l->alpha = 1.0;
    l->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    clear_surface(l->surface, app->background_color);
    
    app->layer_list = g_list_append(app->layer_list, l);
    
    gtk_combo_box_text_append_text(app->layer_combo, l->name);
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->layer_combo), g_list_length(app->layer_list) - 1);
    
    gtk_widget_queue_draw(app->drawing_area);
}

static void on_brush_size_changed(GtkRange *range, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    app->brush_size = gtk_range_get_value(range);
}

static void on_eraser_size_changed(GtkRange *range, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    app->eraser_size = gtk_range_get_value(range);
}

static void on_font_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    GtkWidget *dialog = gtk_font_chooser_dialog_new("Select Font", GTK_WINDOW(app->window));
    gtk_font_chooser_set_font(GTK_FONT_CHOOSER(dialog), app->font_name);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        g_free(app->font_name);
        app->font_name = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(dialog));
    }
    gtk_widget_destroy(dialog);
}

static void on_snap_toggled(GtkToggleButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    app->snap_to_grid = gtk_toggle_button_get_active(btn);
}

static void invert_layers(AppState *app) {
    for (GList *l = app->layer_list; l != NULL; l = l->next) {
        Layer *layer = (Layer *)l->data;
        if (!layer->surface) continue;

        int width = cairo_image_surface_get_width(layer->surface);
        int height = cairo_image_surface_get_height(layer->surface);
        int stride = cairo_image_surface_get_stride(layer->surface);
        unsigned char *data = cairo_image_surface_get_data(layer->surface);

        cairo_surface_flush(layer->surface);
        
        for (int y = 0; y < height; y++) {
            uint32_t *row = (uint32_t *)(data + y * stride);
            for (int x = 0; x < width; x++) {
                uint32_t p = row[x];
                unsigned char a = (p >> 24) & 0xFF;
                if (a == 0) continue; // Skip fully transparent

                unsigned char r = (p >> 16) & 0xFF;
                unsigned char g = (p >> 8) & 0xFF;
                unsigned char b = p & 0xFF;

                // Simple inversion for light/dark transition
                // Black (0,0,0) -> White (255,255,255) and vice versa
                row[x] = (a << 24) | ((255 - r) << 16) | ((255 - g) << 8) | (255 - b);
            }
        }
        cairo_surface_mark_dirty(layer->surface);
    }
}

static void on_dark_mode_toggled(GtkToggleButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    gboolean is_dark = gtk_toggle_button_get_active(btn);
    if (app->dark_mode == is_dark) return;

    app->dark_mode = is_dark;
    
    // Invert existing drawings
    invert_layers(app);

    if (app->dark_mode) {
        app->background_color = make_color(0.1, 0.1, 0.1, 1);
        // Only change current pen color if it's the default black
        if (app->current_color.r == 0 && app->current_color.g == 0 && app->current_color.b == 0) {
            app->current_color = make_color(1, 1, 1, 1);
        }
    } else {
        app->background_color = make_color(1, 1, 1, 1);
        // Only change current pen color if it's the default white
        if (app->current_color.r == 1 && app->current_color.g == 1 && app->current_color.b == 1) {
            app->current_color = make_color(0, 0, 0, 1);
        }
    }
    gtk_widget_queue_draw(app->drawing_area);
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

static void save_project(AppState *app, const char *filename) {
    if (!app->layer_list) return;

    FILE *fp = fopen(filename, "wb");
    if (!fp) return;

    Layer *first = (Layer *)app->layer_list->data;
    
    // Prepare Header
    ProjectHeader header;
    memset(&header, 0, sizeof(header));
    strncpy(header.magic, PROJECT_MAGIC, 8);
    header.version = PROJECT_VERSION;
    header.width = cairo_image_surface_get_width(first->surface);
    header.height = cairo_image_surface_get_height(first->surface);
    header.layer_count = g_list_length(app->layer_list);
    header.active_layer_index = g_list_index(app->layer_list, app->active_layer);
    header.bg_r = app->background_color.r;
    header.bg_g = app->background_color.g;
    header.bg_b = app->background_color.b;
    header.bg_a = app->background_color.a;
    header.page_type = app->current_page_type;
    header.offset_x = app->offset_x;
    header.offset_y = app->offset_y;
    header.scale = app->scale;

    fwrite(&header, sizeof(header), 1, fp);

    // Write Layers
    for (GList *l = app->layer_list; l != NULL; l = l->next) {
        Layer *layer = (Layer *)l->data;
        MemBuffer buf = {0};
        cairo_surface_write_to_png_stream(layer->surface, write_to_buffer, &buf);
        
        uint64_t size = buf.size; // Write size first
        fwrite(&size, sizeof(size), 1, fp);
        fwrite(buf.data, 1, buf.size, fp);
        
        free(buf.data);
    }

    fclose(fp);
}

static void export_canvas(AppState *app, const char *filename) {
    if (!app->layer_list) return;
    
    Layer *first = (Layer *)app->layer_list->data;
    int w = cairo_image_surface_get_width(first->surface);
    int h = cairo_image_surface_get_height(first->surface);
    
    cairo_surface_t *export_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(export_surf);
    
    // Background color (solid for export)
    cairo_set_source_rgba(cr, app->background_color.r, app->background_color.g, app->background_color.b, app->background_color.a);
    cairo_paint(cr);
    
    // Layers
    for (GList *l = app->layer_list; l != NULL; l = l->next) {
        Layer *layer = (Layer *)l->data;
        if (layer->visible && layer->surface) {
            cairo_set_source_surface(cr, layer->surface, 0, 0);
            cairo_paint_with_alpha(cr, layer->alpha);
        }
    }
    
    cairo_destroy(cr);
    cairo_surface_write_to_png(export_surf, filename);
    cairo_surface_destroy(export_surf);
}

static void load_project(AppState *app, const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return;

    ProjectHeader header;
    if (fread(&header, sizeof(header), 1, fp) != 1) { fclose(fp); return; }

    if (strncmp(header.magic, PROJECT_MAGIC, 8) != 0 || header.version != PROJECT_VERSION) {
        // Invalid file or version mismatch
        fclose(fp);
        return;
    }

    // Restore App State
    app->background_color = make_color(header.bg_r, header.bg_g, header.bg_b, header.bg_a);
    app->current_page_type = header.page_type;
    app->offset_x = header.offset_x;
    app->offset_y = header.offset_y;
    app->scale = header.scale;
    
    // Clear current layers
    if (app->layer_list) {
        for (GList *l = app->layer_list; l != NULL; l = l->next) {
            Layer *layer = (Layer *)l->data;
            cairo_surface_destroy(layer->surface);
            g_free(layer->name);
            free(layer);
        }
        g_list_free(app->layer_list);
        app->layer_list = NULL;
    }
    gtk_combo_box_text_remove_all(app->layer_combo);

    // Read Layers
    for (int i = 0; i < header.layer_count; i++) {
        uint64_t size;
        if (fread(&size, sizeof(size), 1, fp) != 1) break;
        
        MemBuffer buf = {0};
        buf.size = size;
        buf.data = malloc(size);
        if (fread(buf.data, 1, size, fp) != size) { free(buf.data); break; }
        
        Layer *l = malloc(sizeof(Layer));
        l->name = g_strdup_printf("Layer %d", i + 1);
        l->visible = TRUE;
        l->alpha = 1.0;
        l->surface = cairo_image_surface_create_from_png_stream(read_from_buffer, &buf);
        free(buf.data);
        
        app->layer_list = g_list_append(app->layer_list, l);
        gtk_combo_box_text_append_text(app->layer_combo, l->name);
    }
    
    app->active_layer = (Layer *)g_list_nth_data(app->layer_list, header.active_layer_index);
    if (!app->active_layer) app->active_layer = (Layer *)app->layer_list->data;
    app->surface = app->active_layer->surface;
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->layer_combo), g_list_index(app->layer_list, app->active_layer));

    // Re-create temp surface
    if (app->temp_surface) cairo_surface_destroy(app->temp_surface);
    app->temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, header.width, header.height);
    clear_temp_surface(app);

    fclose(fp);
    
    // Clear history on load
    app->history_index = -1;
    app->history_max = -1;
    save_history(app); // Save initial state of loaded project
    
    gtk_widget_queue_draw(app->drawing_area);
}

static void on_save_project_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)btn;
    
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Save Project", GTK_WINDOW(app->window),
                                                    GTK_FILE_CHOOSER_ACTION_SAVE,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Save", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    gtk_file_chooser_set_current_name(chooser, "project.sphy");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(chooser);
        save_project(app, filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_open_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)btn;
    
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Open Project", GTK_WINDOW(app->window),
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Open", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Splashy Projects (*.sphy)");
    gtk_file_filter_add_pattern(filter, "*.sphy");
    gtk_file_chooser_add_filter(chooser, filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(chooser);
        load_project(app, filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_save_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)btn;

    GtkWidget *dialog;
    GtkFileChooser *chooser;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;
    gint res;

    dialog = gtk_file_chooser_dialog_new("Export Image",
                                         GTK_WINDOW(app->window),
                                         action,
                                         "_Cancel",
                                         GTK_RESPONSE_CANCEL,
                                         "_Export",
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

static void export_pdf(AppState *app, const char *filename) {
    if (!app->layer_list) return;

    Layer *first = (Layer *)app->layer_list->data;
    int w = cairo_image_surface_get_width(first->surface);
    int h = cairo_image_surface_get_height(first->surface);

    cairo_surface_t *pdf_surf = cairo_pdf_surface_create(filename, w, h);
    cairo_t *cr = cairo_create(pdf_surf);

    // Background
    cairo_set_source_rgba(cr, app->background_color.r, app->background_color.g, app->background_color.b, app->background_color.a);
    cairo_paint(cr);

    // Layers
    for (GList *l = app->layer_list; l != NULL; l = l->next) {
        Layer *layer = (Layer *)l->data;
        if (layer->visible && layer->surface) {
            cairo_set_source_surface(cr, layer->surface, 0, 0);
            cairo_paint_with_alpha(cr, layer->alpha);
        }
    }

    cairo_destroy(cr);
    cairo_surface_destroy(pdf_surf);
}

static void on_export_pdf_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)btn;

    GtkWidget *dialog = gtk_file_chooser_dialog_new("Export PDF",
                                         GTK_WINDOW(app->window),
                                         GTK_FILE_CHOOSER_ACTION_SAVE,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Export", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    gtk_file_chooser_set_current_name(chooser, "drawing.pdf");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(chooser);
        export_pdf(app, filename);
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
    // Set a consistent width and prevent horizontal expansion
    gtk_widget_set_size_request(scrolled, 180, -1);
    gtk_widget_set_hexpand(scrolled, FALSE);
    gtk_widget_set_halign(scrolled, GTK_ALIGN_START);

    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    g_object_set(sidebar, "margin", 8, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), sidebar);

    // Tools - 4-column Grid for compactness
    GtkWidget *tools_frame = gtk_frame_new("Tools");
    GtkWidget *tools_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(tools_grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(tools_grid), 2);
    gtk_grid_set_column_homogeneous(GTK_GRID(tools_grid), TRUE);
    g_object_set(tools_grid, "margin", 5, NULL);
    
    const char *tool_icons[] = {"🖊️", "🧼", "🖍️", "🪣", "⛶", "📏", "⬜", "◯", "△", "⭐", "↗️", "𝐓"};
    const char *tool_tips[] = {"Pen", "Eraser", "Highlighter", "Fill", "Select", "Line", "Rectangle", "Circle", "Triangle", "Star", "Arrow", "Text"};
    
    for (int i = 0; i < TOOL_COUNT; i++) {
        GtkWidget *btn = gtk_toggle_button_new_with_label(tool_icons[i]);
        gtk_widget_set_tooltip_text(btn, tool_tips[i]);
        g_object_set_data(G_OBJECT(btn), "app_ptr", app);
        g_object_set_data(G_OBJECT(btn), "tool_id", GINT_TO_POINTER(i));
        g_signal_connect(btn, "toggled", G_CALLBACK(on_tool_toggled), NULL);
        
        int row = i / 4;
        int col = i % 4;
        gtk_grid_attach(GTK_GRID(tools_grid), btn, col, row, 1, 1);
        app->tool_buttons[i] = btn;
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->tool_buttons[0]), TRUE);
    
    gtk_container_add(GTK_CONTAINER(tools_frame), tools_grid);
    gtk_box_pack_start(GTK_BOX(sidebar), tools_frame, FALSE, FALSE, 0);

    // Page Style & Layers
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

    GtkWidget *layer_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget *layer_combo = gtk_combo_box_text_new();
    app->layer_combo = GTK_COMBO_BOX_TEXT(layer_combo);
    gtk_combo_box_text_append_text(app->layer_combo, "Layer 1");
    gtk_combo_box_set_active(GTK_COMBO_BOX(layer_combo), 0);
    g_signal_connect(layer_combo, "changed", G_CALLBACK(on_layer_combo_changed), app);
    gtk_box_pack_start(GTK_BOX(layer_box), layer_combo, TRUE, TRUE, 0);

    GtkWidget *add_layer_btn = gtk_button_new_with_label("+");
    gtk_widget_set_tooltip_text(add_layer_btn, "Add Layer");
    g_signal_connect(add_layer_btn, "clicked", G_CALLBACK(on_add_layer_clicked), app);
    gtk_box_pack_start(GTK_BOX(layer_box), add_layer_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(style_box), layer_box, FALSE, FALSE, 0);

    GtkWidget *opt_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(opt_grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(opt_grid), 5);

    GtkWidget *snap_toggle = gtk_check_button_new_with_label("Snap");
    g_signal_connect(snap_toggle, "toggled", G_CALLBACK(on_snap_toggled), app);
    gtk_grid_attach(GTK_GRID(opt_grid), snap_toggle, 0, 0, 1, 1);

    GtkWidget *dark_toggle = gtk_check_button_new_with_label("Dark");
    g_signal_connect(dark_toggle, "toggled", G_CALLBACK(on_dark_mode_toggled), app);
    gtk_grid_attach(GTK_GRID(opt_grid), dark_toggle, 1, 0, 1, 1);
    gtk_box_pack_start(GTK_BOX(style_box), opt_grid, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(style_frame), style_box);
    gtk_box_pack_start(GTK_BOX(sidebar), style_frame, FALSE, FALSE, 0);

    // Brush Size & Font
    GtkWidget *brush_frame = gtk_frame_new("Brush & Font");
    GtkWidget *brush_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    g_object_set(brush_box, "margin", 5, NULL);
    
    GtkWidget *sz_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(sz_grid), 5);
    
    gtk_grid_attach(GTK_GRID(sz_grid), gtk_label_new("Pen"), 0, 0, 1, 1);
    app->brush_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 50, 1);
    gtk_widget_set_hexpand(app->brush_scale, TRUE);
    gtk_range_set_value(GTK_RANGE(app->brush_scale), app->brush_size);
    g_signal_connect(app->brush_scale, "value-changed", G_CALLBACK(on_brush_size_changed), app);
    gtk_grid_attach(GTK_GRID(sz_grid), app->brush_scale, 1, 0, 1, 1);
    
    gtk_grid_attach(GTK_GRID(sz_grid), gtk_label_new("Era"), 0, 1, 1, 1);
    app->eraser_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 5, 100, 5);
    gtk_widget_set_hexpand(app->eraser_scale, TRUE);
    gtk_range_set_value(GTK_RANGE(app->eraser_scale), app->eraser_size);
    g_signal_connect(app->eraser_scale, "value-changed", G_CALLBACK(on_eraser_size_changed), app);
    gtk_grid_attach(GTK_GRID(sz_grid), app->eraser_scale, 1, 1, 1, 1);

    gtk_box_pack_start(GTK_BOX(brush_box), sz_grid, FALSE, FALSE, 0);

    GtkWidget *font_btn = gtk_button_new_with_label("Select Font");
    g_signal_connect(font_btn, "clicked", G_CALLBACK(on_font_clicked), app);
    gtk_box_pack_start(GTK_BOX(brush_box), font_btn, FALSE, FALSE, 2);

    gtk_container_add(GTK_CONTAINER(brush_frame), brush_box);
    gtk_box_pack_start(GTK_BOX(sidebar), brush_frame, FALSE, FALSE, 0);

    // Colors
    GtkWidget *colors_frame = gtk_frame_new("Colors");
    GtkWidget *colors_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    g_object_set(colors_box, "margin", 5, NULL);
    
    GtkWidget *custom_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(custom_grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(custom_grid), 5);

    gtk_grid_attach(GTK_GRID(custom_grid), gtk_label_new("Pen:"), 0, 0, 1, 1);
    GtkWidget *pen_color_btn = gtk_color_button_new_with_rgba(&(GdkRGBA){0,0,0,1});
    g_signal_connect(pen_color_btn, "color-set", G_CALLBACK(on_custom_color_clicked), app);
    gtk_grid_attach(GTK_GRID(custom_grid), pen_color_btn, 1, 0, 1, 1);

    GtkWidget *quick_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
    double quick_colors[][4] = {{0,0,0,1}, {1,0,0,1}, {0,0.7,0,1}, {0,0,1,1}, {1,1,0,1}};
    for (int i=0; i<5; i++) {
        GtkWidget *q_btn = create_color_button(app, quick_colors[i][0], quick_colors[i][1], quick_colors[i][2], quick_colors[i][3]);
        gtk_widget_set_size_request(q_btn, 18, 18);
        gtk_box_pack_start(GTK_BOX(quick_box), q_btn, FALSE, FALSE, 0);
    }
    gtk_grid_attach(GTK_GRID(custom_grid), quick_box, 2, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(custom_grid), gtk_label_new("BG:"), 0, 1, 1, 1);
    GtkWidget *bg_color_btn = gtk_color_button_new_with_rgba(&(GdkRGBA){1,1,1,1});
    g_signal_connect(bg_color_btn, "color-set", G_CALLBACK(on_background_color_clicked), app);
    gtk_grid_attach(GTK_GRID(custom_grid), bg_color_btn, 1, 1, 1, 1);

    gtk_box_pack_start(GTK_BOX(colors_box), custom_grid, FALSE, FALSE, 0);
    
    gtk_container_add(GTK_CONTAINER(colors_frame), colors_box);
    gtk_box_pack_start(GTK_BOX(sidebar), colors_frame, FALSE, FALSE, 0);

    // Actions
    GtkWidget *act_frame = gtk_frame_new("Actions");
    GtkWidget *act_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    g_object_set(act_box, "margin", 5, NULL);
    
    GtkWidget *undo_redo_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget *undo_btn = gtk_button_new_with_label("Undo");
    g_signal_connect(undo_btn, "clicked", G_CALLBACK(on_undo_clicked), app);
    gtk_box_pack_start(GTK_BOX(undo_redo_box), undo_btn, TRUE, TRUE, 0);

    GtkWidget *redo_btn = gtk_button_new_with_label("Redo");
    g_signal_connect(redo_btn, "clicked", G_CALLBACK(on_redo_clicked), app);
    gtk_box_pack_start(GTK_BOX(undo_redo_box), redo_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(act_box), undo_redo_box, FALSE, FALSE, 0);

    GtkWidget *file_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget *open_btn = gtk_button_new_with_label("Open");
    g_signal_connect(open_btn, "clicked", G_CALLBACK(on_open_clicked), app);
    gtk_box_pack_start(GTK_BOX(file_box), open_btn, TRUE, TRUE, 0);

    GtkWidget *save_proj_btn = gtk_button_new_with_label("Save");
    g_signal_connect(save_proj_btn, "clicked", G_CALLBACK(on_save_project_clicked), app);
    gtk_box_pack_start(GTK_BOX(file_box), save_proj_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(act_box), file_box, FALSE, FALSE, 0);

    GtkWidget *misc_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget *export_btn = gtk_button_new_with_label("PNG");
    g_signal_connect(export_btn, "clicked", G_CALLBACK(on_save_clicked), app);
    gtk_box_pack_start(GTK_BOX(misc_box), export_btn, TRUE, TRUE, 0);

    GtkWidget *pdf_btn = gtk_button_new_with_label("PDF");
    g_signal_connect(pdf_btn, "clicked", G_CALLBACK(on_export_pdf_clicked), app);
    gtk_box_pack_start(GTK_BOX(misc_box), pdf_btn, TRUE, TRUE, 0);

    GtkWidget *clr_btn = gtk_button_new_with_label("Clear");
    g_signal_connect(clr_btn, "clicked", G_CALLBACK(on_clear_clicked), app);
    gtk_box_pack_start(GTK_BOX(misc_box), clr_btn, TRUE, TRUE, 0);
    
    gtk_box_pack_start(GTK_BOX(act_box), misc_box, FALSE, FALSE, 0);
    
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
    app->font_name = g_strdup("Sans 12");
    app->snap_to_grid = FALSE;
    app->dark_mode = FALSE;
    app->offset_x = 0.0;
    app->offset_y = 0.0;
    app->scale = 1.0;
    app->panning = FALSE;
    app->layer_list = NULL;
    app->active_layer = NULL;
    app->surface = NULL;
    app->temp_surface = NULL;
    app->selection_surf = NULL;
    app->has_selection = FALSE;
    app->dragging_selection = FALSE;
    app->drawing = FALSE;
    app->point_count = 0;
    app->history_index = -1;
    app->history_max = -1;
    for (int i = 0; i < MAX_UNDO; i++) app->undo_stack[i] = NULL;

    // Window
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "Splashy - Advanced Whiteboard (C)");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1000, 700);
    
    // Set minimum window size to prevent cutting off the sidebar
    GdkGeometry geometry;
    geometry.min_width = 850;
    geometry.min_height = 650;
    gtk_window_set_geometry_hints(GTK_WINDOW(app->window), NULL, &geometry, GDK_HINT_MIN_SIZE);

    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(app->window, "key-press-event", G_CALLBACK(on_key_press), app);

    // Layout
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(app->window), hbox);

    // Drawing Area
    app->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->drawing_area, 600, 400); // Minimum size for canvas
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
    gtk_box_pack_start(GTK_BOX(hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 2); // Reduced padding
    gtk_box_pack_start(GTK_BOX(hbox), app->drawing_area, TRUE, TRUE, 0);

    gtk_widget_show_all(app->window);
    
    gtk_main();
    
    // Cleanup if we exit loop
    if (app->surface) cairo_surface_destroy(app->surface);
    if (app->temp_surface) cairo_surface_destroy(app->temp_surface);
    free(app);

    return 0;
}
