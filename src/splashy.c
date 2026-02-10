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
    TOOL_CIRCLE
} ToolType;

// --- Data Structures ---

typedef struct {
    double r, g, b, a;
} Color;

typedef struct {
    double x, y;
    double pressure;
} Point;

typedef struct {
    GtkWidget *window;
    GtkWidget *drawing_area;
    
    // UI References
    GtkWidget *brush_scale;
    GtkWidget *eraser_scale;
    GtkWidget *tool_buttons[5]; // Indexed by ToolType

    // Cairo Surfaces
    cairo_surface_t *surface;      // Main drawing surface
    cairo_surface_t *temp_surface; // Preview surface for shapes

    // State
    ToolType current_tool;
    Color current_color;
    Color background_color;
    double brush_size;
    double eraser_size;
    gboolean drawing;

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

// --- Helper Functions ---

static Color make_color(double r, double g, double b, double a) {
    Color c = {r, g, b, a};
    return c;
}

static void update_tool_buttons(AppState *app) {
    for (int i = 0; i < 5; i++) {
        if ((ToolType)i == app->current_tool) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->tool_buttons[i]), TRUE);
        } else {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->tool_buttons[i]), FALSE);
        }
    }
}

// --- Drawing Logic ---

static void ensure_surface(AppState *app, int width, int height) {
    if (!app->surface) {
        app->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        clear_surface(app->surface, app->background_color);
        
        app->temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        // temp surface should be transparent initially
        cairo_t *cr = cairo_create(app->temp_surface);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_destroy(cr);
        return;
    }

    int old_w = cairo_image_surface_get_width(app->surface);
    int old_h = cairo_image_surface_get_height(app->surface);

    if (width > old_w || height > old_h) {
        int new_w = (width > old_w) ? width : old_w;
        int new_h = (height > old_h) ? height : old_h;

        cairo_surface_t *new_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_w, new_h);
        clear_surface(new_surf, app->background_color);

        cairo_t *cr = cairo_create(new_surf);
        cairo_set_source_surface(cr, app->surface, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);

        cairo_surface_destroy(app->surface);
        app->surface = new_surf;

        // Resize temp surface too
        cairo_surface_destroy(app->temp_surface);
        app->temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, new_w, new_h);
        cr = cairo_create(app->temp_surface);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_destroy(cr);
    }
}

static void clear_surface(cairo_surface_t *surface, Color col) {
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
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


// --- Event Callbacks ---

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)widget;

    if (app->surface) {
        cairo_set_source_surface(cr, app->surface, 0, 0);
        cairo_paint(cr);
    }
    
    if (app->temp_surface) {
        cairo_set_source_surface(cr, app->temp_surface, 0, 0);
        cairo_paint(cr);
    }

    return FALSE;
}

static gboolean on_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer user_data) {
    (void)widget;
    AppState *app = (AppState *)user_data;
    ensure_surface(app, event->width, event->height);
    return TRUE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    
    if (event->button == GDK_BUTTON_PRIMARY) {
        app->drawing = TRUE;
        
        // Handle Pressure
        double pressure = 1.0;
        GdkDevice *device = gdk_event_get_device((GdkEvent*)event);
        if (device) {
             double p;
             if (gdk_event_get_axis((GdkEvent*)event, GDK_AXIS_PRESSURE, &p)) {
                 pressure = p;
             }
        }
        
        Point p = {event->x, event->y, pressure};
        
        if (app->current_tool == TOOL_PEN || app->current_tool == TOOL_ERASER) {
            // Start smoothing buffer
            app->point_count = 1;
            app->points[0] = p;
            
            // Draw a dot for the initial press
            cairo_t *cr = cairo_create(app->surface);
            if (app->current_tool == TOOL_PEN) {
                cairo_set_source_rgba(cr, app->current_color.r, app->current_color.g, app->current_color.b, app->current_color.a);
                // Adjust size by pressure slightly? Maybe not for initial dot to avoid blobs.
                cairo_set_line_width(cr, app->brush_size * pressure);
            } else {
                cairo_set_source_rgba(cr, app->background_color.r, app->background_color.g, app->background_color.b, app->background_color.a);
                cairo_set_line_width(cr, app->eraser_size);
            }
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            
            cairo_move_to(cr, event->x, event->y);
            cairo_line_to(cr, event->x, event->y);
            cairo_stroke(cr);
            cairo_destroy(cr);
            gtk_widget_queue_draw(widget);
        } else {
            // Shape tools
            app->start_point = p;
            clear_temp_surface(app);
        }
    }
    return TRUE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    
    if (app->drawing && app->surface) {
        // Get pressure
        double pressure = 1.0;
        double x = event->x;
        double y = event->y;

        // Hint handling is needed if we enabled hint mask, but we didn't explicitly.
        // If we want high res input, we should check GdkEvent structure directly.
        
        gdk_event_request_motions(event); // Ask for more events
        
        double p_val;
        if (gdk_event_get_axis((GdkEvent*)event, GDK_AXIS_PRESSURE, &p_val)) {
            pressure = p_val;
        }

        Point curr = {x, y, pressure};

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
                cairo_line_to(cr, x, y);
                cairo_stroke(cr);
            } else if (app->current_tool == TOOL_RECTANGLE) {
                cairo_rectangle(cr, app->start_point.x, app->start_point.y, x - app->start_point.x, y - app->start_point.y);
                cairo_stroke(cr);
            } else if (app->current_tool == TOOL_CIRCLE) {
                double dx = x - app->start_point.x;
                double dy = y - app->start_point.y;
                double r = sqrt(dx*dx + dy*dy);
                cairo_arc(cr, app->start_point.x, app->start_point.y, r, 0, 2 * M_PI);
                cairo_stroke(cr);
            }
            cairo_destroy(cr);
            gtk_widget_queue_draw(widget);
        }
    }
    return TRUE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    
    if (event->button == GDK_BUTTON_PRIMARY && app->drawing) {
        app->drawing = FALSE;

        if (app->current_tool == TOOL_PEN || app->current_tool == TOOL_ERASER) {
             // Finish the line if there are remaining points
             // For simple smoothing, we just leave the last segment as is or connect to the very last point.
             // Connecting to the last point (event->x/y) ensures the line ends exactly where user let go.
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
             // If we have points in buffer, connect the last "midpoint" to the final point.
             if (app->point_count >= 2) {
                 Point p_last = app->points[app->point_count-1];
                 Point p_prev = app->points[app->point_count-2];
                 double mid_x = (p_prev.x + p_last.x) / 2.0;
                 double mid_y = (p_prev.y + p_last.y) / 2.0;
                 
                 cairo_move_to(cr, mid_x, mid_y);
                 cairo_line_to(cr, event->x, event->y);
                 cairo_stroke(cr);
             } else if (app->point_count == 1) {
                 // Just a line to end
                 cairo_move_to(cr, app->points[0].x, app->points[0].y);
                 cairo_line_to(cr, event->x, event->y);
                 cairo_stroke(cr);
             }
             cairo_destroy(cr);
        } else {
            // Commit Shape
            clear_temp_surface(app);
            cairo_t *cr = cairo_create(app->surface);
            cairo_set_source_rgba(cr, app->current_color.r, app->current_color.g, app->current_color.b, app->current_color.a);
            cairo_set_line_width(cr, app->brush_size);

            double x = event->x;
            double y = event->y;

            if (app->current_tool == TOOL_LINE) {
                cairo_move_to(cr, app->start_point.x, app->start_point.y);
                cairo_line_to(cr, x, y);
                cairo_stroke(cr);
            } else if (app->current_tool == TOOL_RECTANGLE) {
                cairo_rectangle(cr, app->start_point.x, app->start_point.y, x - app->start_point.x, y - app->start_point.y);
                cairo_stroke(cr);
            } else if (app->current_tool == TOOL_CIRCLE) {
                double dx = x - app->start_point.x;
                double dy = y - app->start_point.y;
                double r = sqrt(dx*dx + dy*dy);
                cairo_arc(cr, app->start_point.x, app->start_point.y, r, 0, 2 * M_PI);
                cairo_stroke(cr);
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

static void on_custom_color_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)btn;
    GtkWidget *dialog = gtk_color_chooser_dialog_new("Choose Color", GTK_WINDOW(app->window));
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        GdkRGBA c;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog), &c);
        app->current_color = make_color(c.red, c.green, c.blue, c.alpha);
    }
    gtk_widget_destroy(dialog);
}

static void on_background_color_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)btn;
    GtkWidget *dialog = gtk_color_chooser_dialog_new("Choose Background Color", GTK_WINDOW(app->window));
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        GdkRGBA c;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog), &c);
        app->background_color = make_color(c.red, c.green, c.blue, c.alpha);
        
        // Redraw background means clearing canvas usually, or at least repainting base.
        // Python app clears canvas.
        clear_surface(app->surface, app->background_color);
        gtk_widget_queue_draw(app->drawing_area);
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
        
        cairo_surface_write_to_png(app->surface, filename);
        
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

static void on_clear_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)btn;
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
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(sidebar, 200, -1);
    g_object_set(sidebar, "margin", 10, NULL);

    // Tools
    GtkWidget *tools_frame = gtk_frame_new("Tools");
    GtkWidget *tools_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    g_object_set(tools_box, "margin", 10, NULL);
    
    const char *tool_names[] = {"Pen", "Eraser", "Line", "Rectangle", "Circle"};
    for (int i = 0; i < 5; i++) {
        GtkWidget *btn = gtk_toggle_button_new_with_label(tool_names[i]);
        g_object_set_data(G_OBJECT(btn), "app_ptr", app);
        g_object_set_data(G_OBJECT(btn), "tool_id", GINT_TO_POINTER(i));
        g_signal_connect(btn, "toggled", G_CALLBACK(on_tool_toggled), NULL);
        gtk_box_pack_start(GTK_BOX(tools_box), btn, FALSE, FALSE, 0);
        app->tool_buttons[i] = btn;
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->tool_buttons[0]), TRUE); // Pen active
    
    gtk_container_add(GTK_CONTAINER(tools_frame), tools_box);
    gtk_box_pack_start(GTK_BOX(sidebar), tools_frame, FALSE, FALSE, 0);

    // Brush Size
    GtkWidget *brush_frame = gtk_frame_new("Brush Size");
    GtkWidget *brush_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    g_object_set(brush_box, "margin", 10, NULL);
    
    gtk_box_pack_start(GTK_BOX(brush_box), gtk_label_new("Pen Size"), FALSE, FALSE, 0);
    app->brush_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 20, 1);
    gtk_range_set_value(GTK_RANGE(app->brush_scale), app->brush_size);
    g_signal_connect(app->brush_scale, "value-changed", G_CALLBACK(on_brush_size_changed), app);
    gtk_box_pack_start(GTK_BOX(brush_box), app->brush_scale, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(brush_box), gtk_label_new("Eraser Size"), FALSE, FALSE, 0);
    app->eraser_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 5, 50, 5);
    gtk_range_set_value(GTK_RANGE(app->eraser_scale), app->eraser_size);
    g_signal_connect(app->eraser_scale, "value-changed", G_CALLBACK(on_eraser_size_changed), app);
    gtk_box_pack_start(GTK_BOX(brush_box), app->eraser_scale, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(brush_frame), brush_box);
    gtk_box_pack_start(GTK_BOX(sidebar), brush_frame, FALSE, FALSE, 0);

    // Colors
    GtkWidget *colors_frame = gtk_frame_new("Colors");
    GtkWidget *colors_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    g_object_set(colors_box, "margin", 10, NULL);
    
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 2);
    
    // Palette data (simplified from Python)
    double palette[][4] = {
        {0,0,0,1}, {0.4,0.4,0.4,1}, {0.6,0.6,0.6,1}, {1,1,1,1},
        {1,0,0,1}, {0.8,0,0,1}, {0.6,0,0,1}, {1,0.4,0.4,1},
        {0,0,1,1}, {0,0,0.8,1}, {0,0,0.6,1}, {0.4,0.4,1,1},
        {0,0.8,0,1}, {0,0.6,0,1}, {0,0.4,0,1}, {0.4,1,0.4,1},
        {1,1,0,1}, {1,0.8,0,1}, {1,0.6,0,1}, {1,0.4,0,1},
        {0.8,0,0.8,1}, {0.6,0,0.6,1}, {1,0.4,1,1}, {0.8,0.4,0.8,1}
    };
    
    int row = 0, col = 0;
    for (unsigned long i = 0; i < sizeof(palette)/sizeof(palette[0]); i++) {
        GtkWidget *btn = create_color_button(app, palette[i][0], palette[i][1], palette[i][2], palette[i][3]);
        gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
        col++;
        if (col >= 4) { col = 0; row++; }
    }
    
    gtk_box_pack_start(GTK_BOX(colors_box), grid, FALSE, FALSE, 0);
    
    GtkWidget *custom_btn = gtk_button_new_with_label("Custom Color...");
    g_signal_connect(custom_btn, "clicked", G_CALLBACK(on_custom_color_clicked), app);
    gtk_box_pack_start(GTK_BOX(colors_box), custom_btn, FALSE, FALSE, 0);
    
    gtk_container_add(GTK_CONTAINER(colors_frame), colors_box);
    gtk_box_pack_start(GTK_BOX(sidebar), colors_frame, FALSE, FALSE, 0);

    // Background
    GtkWidget *bg_frame = gtk_frame_new("Background");
    GtkWidget *bg_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    g_object_set(bg_box, "margin", 10, NULL);
    
    GtkWidget *bg_btn = gtk_button_new_with_label("Background Color...");
    g_signal_connect(bg_btn, "clicked", G_CALLBACK(on_background_color_clicked), app);
    gtk_box_pack_start(GTK_BOX(bg_box), bg_btn, FALSE, FALSE, 0);
    
    gtk_container_add(GTK_CONTAINER(bg_frame), bg_box);
    gtk_box_pack_start(GTK_BOX(sidebar), bg_frame, FALSE, FALSE, 0);

    // Actions
    GtkWidget *act_frame = gtk_frame_new("Actions");
    GtkWidget *act_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    g_object_set(act_box, "margin", 10, NULL);
    
    GtkWidget *save_btn = gtk_button_new_with_label("💾 Save Canvas");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_clicked), app);
    gtk_box_pack_start(GTK_BOX(act_box), save_btn, FALSE, FALSE, 0);

    GtkWidget *clr_btn = gtk_button_new_with_label("🗑️ Clear Canvas");
    g_signal_connect(clr_btn, "clicked", G_CALLBACK(on_clear_clicked), app);
    gtk_box_pack_start(GTK_BOX(act_box), clr_btn, FALSE, FALSE, 0);
    
    gtk_container_add(GTK_CONTAINER(act_frame), act_box);
    gtk_box_pack_start(GTK_BOX(sidebar), act_frame, FALSE, FALSE, 0);

    return sidebar;
}

// --- Main ---

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    AppState *app = malloc(sizeof(AppState));
    // Defaults
    app->current_tool = TOOL_PEN;
    app->current_color = make_color(0, 0, 0, 1);
    app->background_color = make_color(1, 1, 1, 1);
    app->brush_size = 3.0;
    app->eraser_size = 10.0;
    app->surface = NULL;
    app->temp_surface = NULL;
    app->drawing = FALSE;
    app->point_count = 0;

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
                          GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    
    g_signal_connect(app->drawing_area, "draw", G_CALLBACK(on_draw), app);
    g_signal_connect(app->drawing_area, "configure-event", G_CALLBACK(on_configure), app);
    g_signal_connect(app->drawing_area, "button-press-event", G_CALLBACK(on_button_press), app);
    g_signal_connect(app->drawing_area, "button-release-event", G_CALLBACK(on_button_release), app);
    g_signal_connect(app->drawing_area, "motion-notify-event", G_CALLBACK(on_motion_notify), app);

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
