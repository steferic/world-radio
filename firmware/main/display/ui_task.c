#include "ui_task.h"

#include "ui_draw.h"
#include "lcd_driver.h"

#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/task.h"

static const char *TAG = "ui_task";

// Queue capacity chosen for the worst case: a fast rotary spin can produce
// ~50 events/second, and metadata/nav events are rare. 16 slots gives us
// ~300ms of headroom before the ISR would start dropping, which is well
// beyond a healthy tick period.
#define UI_QUEUE_LEN            16

// Cap the nav stack. Real depth we use is 3 (now_playing -> menu -> child),
// 4 gives one slot of headroom without noticeable RAM cost.
#define UI_STACK_MAX_DEPTH      4

// TICK cadence when the queue is idle. Screens use this to animate; too
// fast wastes CPU on a battery of glyph writes, too slow makes the
// spinner look jerky. 100ms is a good middle.
#define UI_TICK_INTERVAL_MS     100

static QueueHandle_t s_queue = NULL;

// The screen stack. s_stack[0] is the root (never popped away -- if a POP
// would leave it empty, it's ignored). s_top is the index of the active
// screen, or -1 if nothing is installed yet (should only occur before
// ui_task_start finishes).
static const screen_t *s_stack[UI_STACK_MAX_DEPTH];
static int s_top = -1;

// Only called from the UI task -- no locking on s_stack because there's
// exactly one writer.
static void activate_top(void)
{
    if (s_top < 0) return;
    ui_draw_clear_screen();
    if (s_stack[s_top]->enter) {
        s_stack[s_top]->enter();
    }
}

static void deactivate_top(void)
{
    if (s_top < 0) return;
    if (s_stack[s_top]->exit) {
        s_stack[s_top]->exit();
    }
}

static void handle_nav(const ui_event_t *evt)
{
    switch (evt->type) {
    case UI_EVT_REPLACE_SCREEN:
        if (evt->screen == NULL) return;
        // Tear down every screen on the stack, top first, then install the
        // new one as the sole entry. Used for lifecycle transitions where
        // "back" would make no sense (boot -> now_playing).
        while (s_top >= 0) {
            deactivate_top();
            s_top--;
        }
        s_stack[0] = evt->screen;
        s_top = 0;
        activate_top();
        break;

    case UI_EVT_PUSH_SCREEN:
        if (evt->screen == NULL) return;
        if (s_top + 1 >= UI_STACK_MAX_DEPTH) {
            ESP_LOGW(TAG, "screen stack full, ignoring push");
            return;
        }
        // The covered screen stays on the stack; its file-static state is
        // preserved. We do call exit() on it so it can stop any tick-
        // driven animation (a spinner shouldn't keep re-rendering under a
        // menu that's covering it).
        deactivate_top();
        s_top++;
        s_stack[s_top] = evt->screen;
        activate_top();
        break;

    case UI_EVT_POP_SCREEN:
        if (s_top <= 0) {
            // Refuse to pop the root -- there'd be nothing left to show.
            return;
        }
        deactivate_top();
        s_top--;
        // Re-enter the uncovered screen from scratch. It's simpler and
        // more robust than trying to remember exactly which pixels the
        // pushed screen dirtied.
        activate_top();
        break;

    default:
        break;
    }
}

static void ui_task(void *pvParameters)
{
    (void)pvParameters;

    ui_event_t evt;
    for (;;) {
        BaseType_t got = xQueueReceive(s_queue, &evt, pdMS_TO_TICKS(UI_TICK_INTERVAL_MS));

        if (got != pdTRUE) {
            // Idle: synthesize a TICK for the active screen.
            ui_event_t tick = { .type = UI_EVT_TICK, .screen = NULL };
            if (s_top >= 0 && s_stack[s_top]->handle_event) {
                s_stack[s_top]->handle_event(&tick);
            }
            continue;
        }

        switch (evt.type) {
        case UI_EVT_REPLACE_SCREEN:
        case UI_EVT_PUSH_SCREEN:
        case UI_EVT_POP_SCREEN:
            handle_nav(&evt);
            break;

        default:
            // Everything else is delivered to the active screen. Screens
            // that don't care about a given event simply ignore it in
            // their handle_event.
            if (s_top >= 0 && s_stack[s_top]->handle_event) {
                s_stack[s_top]->handle_event(&evt);
            }
            break;
        }
    }
}

esp_err_t ui_task_start(const screen_t *initial)
{
    if (initial == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = lcd_driver_init();
    if (err != ESP_OK) {
        return err;
    }

    s_queue = xQueueCreate(UI_QUEUE_LEN, sizeof(ui_event_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Install the initial screen and draw it before returning, so the
    // caller can rely on something being on screen as soon as this
    // function comes back (typically the "Connecting to Wi-Fi..." screen,
    // rendered before wifi_connect_start's blocking wait).
    s_stack[0] = initial;
    s_top = 0;
    activate_top();

    BaseType_t ok = xTaskCreate(ui_task, "ui_task", 4096, NULL, 3, NULL);
    if (ok != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "UI task started");
    return ESP_OK;
}

void ui_post(const ui_event_t *evt)
{
    if (s_queue == NULL || evt == NULL) return;
    // 0-tick timeout: drop rather than block a producer if the UI is
    // wedged. Better a lost redraw than a stuck HTTP task.
    xQueueSend(s_queue, evt, 0);
}

// IRAM_ATTR because this is called from IRAM_ATTR ISRs in rotary_encoder
// and pushbutton. If it lived in flash, a flash-write elsewhere in the
// system could unmap the page mid-ISR and crash.
void IRAM_ATTR ui_post_from_isr(const ui_event_t *evt, BaseType_t *higher_prio_task_woken)
{
    if (s_queue == NULL || evt == NULL) return;
    xQueueSendFromISR(s_queue, evt, higher_prio_task_woken);
}

void ui_screen_replace(const screen_t *s)
{
    ui_event_t evt = { .type = UI_EVT_REPLACE_SCREEN, .screen = s };
    ui_post(&evt);
}

void ui_screen_push(const screen_t *s)
{
    ui_event_t evt = { .type = UI_EVT_PUSH_SCREEN, .screen = s };
    ui_post(&evt);
}

void ui_screen_pop(void)
{
    ui_event_t evt = { .type = UI_EVT_POP_SCREEN, .screen = NULL };
    ui_post(&evt);
}
