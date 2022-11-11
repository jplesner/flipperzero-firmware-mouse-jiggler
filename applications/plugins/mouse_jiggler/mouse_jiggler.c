#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>

#define MOUSE_MOVE_SHORT 5
#define MOUSE_MOVE_TICKS 500

typedef enum {
    DirectionRight,
    DirectionLeft,
} Direction;

typedef struct {
    bool on;
    Direction direction;
} AppState;

typedef enum {
    EventTypeInput,
    EventTypeTick,
} EventType;

typedef struct {
    union {
        InputEvent input;
    };
    EventType type;
} UsbMouseEvent;

static void mouse_jiggler_render_callback(Canvas* canvas, void* ctx) {
    const AppState* app_state = acquire_mutex((ValueMutex*)ctx, 25);
    if(app_state == NULL) {
        return;
    }
    canvas_clear(canvas); // TODO remove?

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "USB Mouse Jiggler");

    canvas_draw_str(canvas, 37, 31, app_state->on? "ON" : "OFF");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 51, "Press [ok] to toggle");
    canvas_draw_str(canvas, 0, 63, "Press [back] to exit");

    release_mutex((ValueMutex*)ctx, app_state);
}

static void mouse_jiggler_input_callback(InputEvent* input_event, FuriMessageQueue* event_queue) {
    furi_assert(event_queue);

    UsbMouseEvent event = {.type = EventTypeInput, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

static void mouse_jiggler_update_timer_callback(FuriMessageQueue* event_queue) {
    furi_assert(event_queue);

    UsbMouseEvent event = {.type = EventTypeTick};
    furi_message_queue_put(event_queue, &event, 0);
}

static void mouse_jiggler_perform_movement(AppState* const app_state) {
    if (!app_state->on) {
        return;
    }

    if (app_state->direction == DirectionRight)
    {
        furi_hal_hid_mouse_move(MOUSE_MOVE_SHORT, 0);
        app_state->direction = DirectionLeft;
    } else {
        furi_hal_hid_mouse_move(-MOUSE_MOVE_SHORT, 0);
        app_state->direction = DirectionRight;
    }
}

static void mouse_jiggler_init_state(AppState* const app_state) {
    app_state->on = false;
    app_state->direction = DirectionLeft;
}

int32_t mouse_jiggler_app(void* p) {
    UNUSED(p);

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(UsbMouseEvent));
    furi_check(event_queue);

    AppState* app_state = malloc(sizeof(AppState));
    mouse_jiggler_init_state(app_state);

    ValueMutex state_mutex;
    if(!init_mutex(&state_mutex, app_state, sizeof(AppState))) {
        FURI_LOG_E("MouseJiggler", "cannot create mutex\r\n");
        free(app_state);
        return 255;
    }

    ViewPort* view_port = view_port_alloc();
    FuriHalUsbInterface* usb_mode_prev = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    furi_check(furi_hal_usb_set_config(&usb_hid, NULL) == true);

    view_port_draw_callback_set(view_port, mouse_jiggler_render_callback, &state_mutex);
    view_port_input_callback_set(view_port, mouse_jiggler_input_callback, event_queue);

    FuriTimer* timer =
        furi_timer_alloc(mouse_jiggler_update_timer_callback, FuriTimerTypePeriodic, event_queue);
    furi_timer_start(timer, MOUSE_MOVE_TICKS);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    UsbMouseEvent event;
    for(bool processing = true; processing;) {
        FuriStatus event_status = furi_message_queue_get(event_queue, &event, 100);

        AppState* app_state = (AppState*)acquire_mutex_block(&state_mutex);

        if(event_status == FuriStatusOk) { 
            if(event.type == EventTypeInput) {
                if(event.input.key == InputKeyBack) {
                    processing = false;
                } else if (event.input.key == InputKeyOk) {
                    app_state->on ^= true;
                }
            } else if(event.type == EventTypeTick) {
                mouse_jiggler_perform_movement(app_state);
            }
        }

        view_port_update(view_port);
        release_mutex(&state_mutex, app_state);
    }

    furi_hal_usb_set_config(usb_mode_prev, NULL);

    // remove & free all stuff created by app
    furi_timer_free(timer);
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    delete_mutex(&state_mutex);
    free(app_state);

    return 0;
}