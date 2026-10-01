#include <Arduino.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <Adafruit_PN532.h>
#include <Wire.h>
#include "esp_heap_caps.h"

#include "esp_display_panel.hpp"
#include "esp_lv_adapter_arduino.h"
#include "lvgl.h"
#include "app_config.h"
#include "academic_config.h"
#include "data/provisional_data.h"
#include "student_model.h"
#include "network/wifi_manager.h"
#include "time/time_manager.h"
#include "storage/storage_manager.h"
#include "storage/sd_manager.h"

using namespace esp_panel::board;

namespace {
constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 480;
constexpr lv_coord_t GRID_MARGIN = 18;
constexpr lv_coord_t GRID_GAP = 12;
constexpr lv_coord_t HEADER_HEIGHT = 96;
constexpr lv_coord_t NAV_HEIGHT = 50;
constexpr lv_coord_t MONITOR_WIDTH = 112;
constexpr lv_coord_t MONITOR_HEIGHT = 54;
constexpr lv_coord_t MONITOR_SAFE_X = 636;
constexpr lv_coord_t MONITOR_SAFE_Y = 410;
constexpr lv_coord_t WAIT_MONITOR_SAFE_X = 676;
constexpr lv_coord_t WAIT_MONITOR_SAFE_Y = 326;
constexpr lv_coord_t CONFIG_GEAR_SIZE = 32;
constexpr uint32_t CONFIG_GEAR_ROTATION_DEG10 = 900;
constexpr uint32_t CONFIG_GEAR_ROTATION_MS = 220;
constexpr uint32_t CONFIG_GEAR_IDLE_ROTATION_MS = 10000;
// Temporary diagnostic suite. Enable only for an explicitly requested on-device run.
constexpr bool PERFORMANCE_TEST_ENABLED = false;
constexpr uint32_t PERFORMANCE_SCREEN_HOLD_MS = 3000;
constexpr uint32_t INK = 0x183B56;
constexpr uint32_t MUTED = 0x61758A;
constexpr uint32_t SKY = 0xDDF3FF;
constexpr uint32_t BLUE = 0x3289D9;
constexpr uint32_t GREEN = 0x43B97F;
constexpr uint32_t YELLOW = 0xF6C747;
constexpr uint32_t ORANGE = 0xF28B50;
constexpr uint32_t PINK = 0xE982A8;
constexpr uint32_t PURPLE = 0x9274D8;

enum class AppTheme : uint8_t { LIGHT, DARK };
AppTheme current_theme = AppTheme::LIGHT;

struct ThemePalette {
    uint32_t background;
    uint32_t surface;
    uint32_t primary_text;
    uint32_t secondary_text;
    uint32_t border;
};

const ThemePalette LIGHT_PALETTE = {0xEEF6FF, 0xFBFDFF, INK, MUTED, 0xB6D6E8};
const ThemePalette DARK_PALETTE = {0x071522, 0x10283A, 0xF3F7FB, 0xB3C0CC, 0x24506A};

const ThemePalette &theme_palette()
{
    return current_theme == AppTheme::DARK ? DARK_PALETTE : LIGHT_PALETTE;
}

lv_color_t theme_color_hex(uint32_t color)
{
    if (current_theme == AppTheme::DARK) {
        switch (color) {
            case 0xF7FBFF: case 0xF4F8FC: color = DARK_PALETTE.background; break;
            case 0xFFFFFF: color = DARK_PALETTE.surface; break;
            case 0xF2F8FF: color = 0x2A3A4A; break;
            case INK: color = DARK_PALETTE.primary_text; break;
            case MUTED: color = DARK_PALETTE.secondary_text; break;
            case 0xD8EEF7: case 0xBFE8F8: color = DARK_PALETTE.border; break;
            case 0xE8F7FF: case 0xE8F0FF: color = 0x203247; break;
            case 0xDDF3FF: color = 0x1D3B52; break;
            case 0xFFF4D8: color = 0x403521; break;
            case 0xFBEAF2: color = 0x3D2939; break;
            case 0xE4F7EC: color = 0x214033; break;
            case 0xEEE8FF: color = 0x352D4A; break;
            case 0xE3D8F7: color = 0x4A3D61; break;
        }
    }
    return lv_color_make((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

#define lv_color_hex theme_color_hex

std::shared_ptr<Board> board;
lv_obj_t *nfc_message_label = nullptr, *nfc_hint_label = nullptr;
lv_obj_t *clock_label = nullptr, *date_label = nullptr, *motivation_label = nullptr;
lv_obj_t *nfc_card_visual = nullptr, *nfc_ring_outer = nullptr, *nfc_ring_middle = nullptr, *nfc_ring_inner = nullptr;
lv_obj_t *nfc_panel = nullptr;
lv_obj_t *nfc_status_label = nullptr;
lv_obj_t *wifi_status_label = nullptr;
lv_obj_t *wifi_config_state_label = nullptr;
lv_obj_t *wifi_config_ssid_label = nullptr;
lv_obj_t *wifi_config_ip_label = nullptr;
lv_obj_t *wifi_config_rssi_label = nullptr;
bool wifi_ui_needs_refresh = true;
lv_obj_t *wifi_scan_status_label = nullptr;
lv_obj_t *wifi_scan_list = nullptr;
lv_obj_t *wifi_password_textarea = nullptr;
lv_obj_t *wifi_manual_ssid_textarea = nullptr;
lv_obj_t *wifi_keyboard = nullptr;
lv_obj_t *wifi_connect_status_label = nullptr;
String wifi_selected_ssid;
bool wifi_selected_secured = false;
bool wifi_manual_network = false;
uint32_t wifi_rendered_scan_revision = UINT32_MAX;
const char *storage_status_message = nullptr;
bool storage_status_success = false;
struct BootProgressState {
    uint8_t percent = 0;
    char message[72] = "";
};
BootProgressState boot_progress_state;
lv_obj_t *boot_splash_screen = nullptr;
lv_obj_t *boot_progress_bar = nullptr;
lv_obj_t *boot_percent_label = nullptr;
lv_obj_t *boot_state_label = nullptr;
lv_obj_t *boot_detail_label = nullptr;
lv_obj_t *boot_logo_arc = nullptr;
lv_obj_t *boot_deferred_screen = nullptr;
lv_timer_t *boot_progress_timer = nullptr;
lv_timer_t *session_timeout_lv_timer = nullptr;
uint8_t boot_bar_current_value = 0;
uint32_t boot_wifi_started_ms = 0;
uint32_t boot_ready_since_ms = 0;
uint32_t boot_last_detail_update_ms = 0;
char boot_detail_text[112] = "";
bool boot_adapter_started = false;
bool boot_setup_complete = false;
bool boot_wifi_resolved = false;
bool boot_ui_prepared = false;
bool boot_splash_active = false;
bool boot_defer_screen_load = false;
lv_obj_t *waiting_indicator = nullptr;
lv_obj_t *balance_amount_label = nullptr;
lv_obj_t *balance_unit_label = nullptr;
lv_obj_t *performance_overlay = nullptr;
lv_obj_t *performance_label = nullptr;
lv_timer_t *performance_timer = nullptr;
lv_obj_t *config_gear_canvas = nullptr;
bool config_gear_animating = false;
int32_t config_gear_angle = 0;
lv_color_t config_gear_canvas_buffer[CONFIG_GEAR_SIZE * CONFIG_GEAR_SIZE];
lv_timer_t *transition_release_timer = nullptr;
bool transition_in_progress = false;
bool performance_monitor_enabled = false;
bool visual_effects_enabled = true;
uint32_t performance_last_frame_count = 0;
uint32_t performance_last_sample_ms = 0;
uint32_t performance_transition_started_ms = 0;
uint32_t performance_screen_create_ms = 0;
uint32_t performance_transition_ms = 0;
lv_timer_t *performance_test_timer = nullptr;
uint8_t performance_test_scenario = 0;
uint8_t performance_test_step = 0;
struct PerformanceScreenStats {
    uint32_t samples = 0;
    uint32_t fps_min = UINT32_MAX;
    uint32_t fps_max = 0;
    uint64_t fps_sum = 0;
    uint32_t heap_min = UINT32_MAX;
    size_t psram_free_min = SIZE_MAX;
    uint32_t create_ms = 0;
    uint32_t transition_ms = 0;
};
PerformanceScreenStats performance_stats[24];
uint8_t motivation_index = 0;

// MODO DEMOSTRACION TEMPORAL: tocar la tarjeta simula un alumno identificado.
// Cambiar a false cuando el flujo de identificación real del PN532 sea la fuente de sesión.
constexpr bool DEMO_MODE = true;

enum Pantalla : uint8_t {
    PANTALLA_ESPERA,
    PANTALLA_DETECCION,
    PANTALLA_ALUMNO,
    PANTALLA_CUENTA,
    PANTALLA_PROGRESO,
    PANTALLA_FLUIDEZ,
    PANTALLA_DICTADO,
    PANTALLA_METAS,
    PANTALLA_LOGROS,
    PANTALLA_ENTRADA,
    PANTALLA_CONFIGURACION,
    PANTALLA_WIFI_REDES,
    PANTALLA_WIFI_PASSWORD,
    PANTALLA_WIFI_PROGRESS,
    PANTALLA_WIFI_RESULT,
    PANTALLA_WIFI_OLVIDAR,
    PANTALLA_STORAGE_LOCAL,
    PANTALLA_STORAGE_CONFIRMAR,
    PANTALLA_STORAGE_CONFIRMAR_FINAL,
    PANTALLA_TRANSFERIR,
    PANTALLA_TRANSFER_TECLADO,
    PANTALLA_TRANSFER_CONFIRMAR,
    PANTALLA_TRANSFER_RESULTADO
};

Pantalla pantalla_actual = PANTALLA_ESPERA;
Pantalla performance_transition_from = PANTALLA_ESPERA;
Pantalla performance_transition_to = PANTALLA_ESPERA;
lv_timer_t *demo_detection_timer = nullptr;
uint32_t last_student_activity_ms = 0;

const Student *selected_student = getDemoStudent();
uint16_t transfer_sender_id = 0;
uint16_t transfer_receiver_id = 0;
uint32_t transfer_amount = 0;
bool transfer_receiver_detected = false;
bool transfer_balance_warning = false;
char transfer_keypad_buffer[8] = {};
lv_obj_t *transfer_feedback_label = nullptr;
lv_obj_t *transfer_keypad_input_label = nullptr;

const char *student_short_name(const Student *student)
{
    if (student == nullptr) return "Alumno";
    if (student->preferred_name != nullptr) return student->preferred_name;
    const char *last_space = strrchr(student->name ? student->name : "", ' ');
    return last_space ? last_space + 1 : (student->name ? student->name : "Alumno");
}

void mostrar_pantalla(Pantalla pantalla);
void demo_menu_event(lv_event_t *event);
void demo_back_event(lv_event_t *event);
void demo_exit_event(lv_event_t *event);
void config_event(lv_event_t *event);
void config_theme_event(lv_event_t *event);
void config_performance_event(lv_event_t *event);
void config_effects_event(lv_event_t *event);
void config_wifi_event(lv_event_t *event);
void config_storage_event(lv_event_t *event);
void storage_back_event(lv_event_t *event);
void storage_delete_event(lv_event_t *event);
void storage_confirm_continue_event(lv_event_t *event);
void storage_confirm_cancel_event(lv_event_t *event);
void storage_delete_final_event(lv_event_t *event);
void create_wifi_page_header(lv_obj_t *screen, const char *title);
void wifi_scan_event(lv_event_t *event);
void wifi_other_network_event(lv_event_t *event);
void wifi_forget_event(lv_event_t *event);
void wifi_forget_confirm_event(lv_event_t *event);
void wifi_network_select_event(lv_event_t *event);
void wifi_password_cancel_event(lv_event_t *event);
void wifi_connect_event(lv_event_t *event);
void wifi_textarea_focus_event(lv_event_t *event);
void startup_progress_timer_cb(lv_timer_t *timer);

#define WIFI_KB_POPOVER(width) (LV_BTNMATRIX_CTRL_POPOVER | (width))
#define WIFI_KB_ACTION(width) (LV_KEYBOARD_CTRL_BTN_FLAGS | (width))

static const char *wifi_kb_map_lower[] = {
    "123", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "BORRAR", "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", "ENTER", "\n",
    "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
    "TECLADO", "IZQ", " ", "DER", "OK", ""
};

static const lv_btnmatrix_ctrl_t wifi_kb_ctrl_lower[] = {
    WIFI_KB_ACTION(5), WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4),
    WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4), WIFI_KB_ACTION(7),
    WIFI_KB_ACTION(6), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3),
    WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_ACTION(7),
    WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1),
    WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1),
    WIFI_KB_ACTION(3), WIFI_KB_ACTION(2), WIFI_KB_POPOVER(6), WIFI_KB_ACTION(2), WIFI_KB_ACTION(3)
};

static const char *wifi_kb_map_upper[] = {
    "123", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "BORRAR", "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", "ENTER", "\n",
    "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
    "TECLADO", "IZQ", " ", "DER", "OK", ""
};

static const lv_btnmatrix_ctrl_t wifi_kb_ctrl_upper[] = {
    WIFI_KB_ACTION(5), WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4),
    WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4), WIFI_KB_POPOVER(4), WIFI_KB_ACTION(7),
    WIFI_KB_ACTION(6), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3),
    WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_ACTION(7),
    WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1),
    WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1),
    WIFI_KB_ACTION(3), WIFI_KB_ACTION(2), WIFI_KB_POPOVER(6), WIFI_KB_ACTION(2), WIFI_KB_ACTION(3)
};

static const char *wifi_kb_map_special[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "BORRAR", "\n",
    "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
    "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
    "ABC", "IZQ", " ", "DER", "OK", ""
};

static const lv_btnmatrix_ctrl_t wifi_kb_ctrl_special[] = {
    WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1),
    WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_ACTION(2),
    WIFI_KB_ACTION(5), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3),
    WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3), WIFI_KB_POPOVER(3),
    WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1),
    WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1), WIFI_KB_POPOVER(1),
    WIFI_KB_ACTION(3), WIFI_KB_ACTION(2), WIFI_KB_POPOVER(6), WIFI_KB_ACTION(2), WIFI_KB_ACTION(3)
};

void wifi_keyboard_ascii_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *keyboard = lv_event_get_target(event);
    lv_obj_t *textarea = lv_keyboard_get_textarea(keyboard);
    const uint16_t button = lv_btnmatrix_get_selected_btn(keyboard);
    const char *text = lv_keyboard_get_btn_text(keyboard, button);
    if (text == nullptr) return;

    if (strcmp(text, "ABC") == 0) {
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER);
    } else if (strcmp(text, "abc") == 0) {
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    } else if (strcmp(text, "123") == 0) {
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_SPECIAL);
    } else if (strcmp(text, "TECLADO") == 0) {
        lv_event_send(keyboard, LV_EVENT_CANCEL, nullptr);
        if (textarea) lv_event_send(textarea, LV_EVENT_CANCEL, nullptr);
    } else if (textarea && strcmp(text, "BORRAR") == 0) {
        lv_textarea_del_char(textarea);
    } else if (textarea && strcmp(text, "IZQ") == 0) {
        lv_textarea_cursor_left(textarea);
    } else if (textarea && strcmp(text, "DER") == 0) {
        lv_textarea_cursor_right(textarea);
    } else if (textarea && strcmp(text, "OK") == 0) {
        lv_event_send(keyboard, LV_EVENT_READY, nullptr);
        lv_event_send(textarea, LV_EVENT_READY, nullptr);
    } else if (textarea && strcmp(text, "ENTER") == 0) {
        lv_textarea_add_char(textarea, '\n');
        if (lv_textarea_get_one_line(textarea)) lv_event_send(textarea, LV_EVENT_READY, nullptr);
    } else if (textarea) {
        lv_textarea_add_text(textarea, text);
    }
}

#undef WIFI_KB_POPOVER
#undef WIFI_KB_ACTION
void wifi_progress_cancel_event(lv_event_t *event);
void wifi_retry_event(lv_event_t *event);
void wifi_change_network_event(lv_event_t *event);
void wifi_result_cancel_event(lv_event_t *event);
void wifi_result_accept_event(lv_event_t *event);
void transfer_menu_event(lv_event_t *event);
void transfer_receiver_event(lv_event_t *event);
void transfer_amount_event(lv_event_t *event);
void transfer_keypad_event(lv_event_t *event);
void transfer_continue_event(lv_event_t *event);
void transfer_confirm_event(lv_event_t *event);
void transfer_cancel_event(lv_event_t *event);
lv_obj_t *make_content_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                             lv_coord_t width, lv_coord_t height, uint32_t color);
void create_date_clock_card(lv_obj_t *parent, lv_coord_t width, lv_coord_t height,
                            lv_coord_t right_offset, lv_coord_t top_offset);
void update_performance_overlay(lv_timer_t *timer);
void performance_test_step_cb(lv_timer_t *timer);
const char *performance_screen_name(Pantalla screen);
void update_wifi_ui();
void update_wifi_setup_ui();

constexpr bool PN532_ENABLED = false;
constexpr int PN532_SDA = 43;
constexpr int PN532_SCL = 44;
constexpr uint8_t PN532_EXPECTED_ADDRESS = 0x24;
static_assert(PN532_I2C_ADDRESS == PN532_EXPECTED_ADDRESS, "Unexpected PN532 I2C address");

TwoWire pn532_wire(1);
Adafruit_PN532 *pn532 = nullptr;

enum NfcState : uint8_t { NFC_INITIALIZING, NFC_READY, NFC_NO_DEVICE, NFC_CARD_DETECTED };
volatile NfcState nfc_state = NFC_INITIALIZING;
volatile uint32_t nfc_detection_serial = 0;
volatile bool nfc_card_latched = false;
uint8_t nfc_uid[7] = {};
uint8_t nfc_uid_length = 0;
portMUX_TYPE nfc_mux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t nfc_task_handle = nullptr;

void formatear_fecha_hora(const struct tm &local_time, char *date_buffer, size_t date_size,
                          char *time_buffer, size_t time_size)
{
    static const char *const weekdays[] = {
        "domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado"
    };
    static const char *const months[] = {
        "enero", "febrero", "marzo", "abril", "mayo", "junio",
        "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"
    };
    const char *weekday = local_time.tm_wday >= 0 && local_time.tm_wday < 7
                              ? weekdays[local_time.tm_wday]
                              : "";
    const char *month = local_time.tm_mon >= 0 && local_time.tm_mon < 12
                            ? months[local_time.tm_mon]
                            : "";
    snprintf(date_buffer, date_size, "%s %d de %s de %d", weekday, local_time.tm_mday,
             month, local_time.tm_year + 1900);
    int hour_12 = local_time.tm_hour % 12;
    if (hour_12 == 0) hour_12 = 12;
    const char *period = local_time.tm_hour < 12 ? "a.m." : "p.m.";
    snprintf(time_buffer, time_size, "%d:%02d:%02d %s", hour_12, local_time.tm_min,
             local_time.tm_sec, period);
}

void obtener_texto_fecha_hora(char *date_buffer, size_t date_size,
                              char *time_buffer, size_t time_size)
{
    struct tm local_time = {};
    if (!time_manager.getLocalTime(local_time)) {
        snprintf(date_buffer, date_size, "--/--/----");
        snprintf(time_buffer, time_size, "--:--:--");
        return;
    }
    formatear_fecha_hora(local_time, date_buffer, date_size, time_buffer, time_size);
}
void halt_on_error() { while (true) vTaskDelay(pdMS_TO_TICKS(1000)); }

void boot_bar_animation_exec(void *object, int32_t value)
{
    lv_obj_t *bar = static_cast<lv_obj_t *>(object);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    boot_bar_current_value = static_cast<uint8_t>(value);
}

void boot_logo_animation_exec(void *object, int32_t value)
{
    lv_arc_set_value(static_cast<lv_obj_t *>(object), value);
}

void set_boot_progress(uint8_t percent, const char *message)
{
    if (percent > 100) percent = 100;
    if (percent < boot_progress_state.percent) return;

    const bool percent_changed = percent != boot_progress_state.percent;
    const bool message_changed = strcmp(boot_progress_state.message, message) != 0;
    if (!percent_changed && !message_changed) return;

    boot_progress_state.percent = percent;
    strncpy(boot_progress_state.message, message, sizeof(boot_progress_state.message) - 1);
    boot_progress_state.message[sizeof(boot_progress_state.message) - 1] = '\0';
    Serial.printf("[Boot] %u%% %s\n", static_cast<unsigned>(percent), message);

    if (!boot_adapter_started || boot_progress_bar == nullptr) return;
    if (esp_lv_adapter_lock(1000) != ESP_OK) return;

    if (percent_changed) {
        lv_anim_del(boot_progress_bar, boot_bar_animation_exec);
        lv_anim_t bar_anim;
        lv_anim_init(&bar_anim);
        lv_anim_set_var(&bar_anim, boot_progress_bar);
        lv_anim_set_values(&bar_anim, boot_bar_current_value, percent);
        lv_anim_set_time(&bar_anim, 220);
        lv_anim_set_path_cb(&bar_anim, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&bar_anim, boot_bar_animation_exec);
        lv_anim_start(&bar_anim);
    }

    char percent_text[8];
    snprintf(percent_text, sizeof(percent_text), "%u%%", static_cast<unsigned>(percent));
    lv_label_set_text(boot_percent_label, percent_text);
    lv_label_set_text(boot_state_label, boot_progress_state.message);
    esp_lv_adapter_unlock();
}

void create_boot_splash()
{
    boot_splash_screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(boot_splash_screen);
    lv_obj_set_size(boot_splash_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(boot_splash_screen, lv_color_hex(theme_palette().background), 0);
    lv_obj_set_style_bg_opa(boot_splash_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(boot_splash_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(boot_splash_screen);
    lv_label_set_text(title, "BANCO ESCOLAR");
    lv_obj_set_style_text_font(title, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(theme_palette().primary_text), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t *subtitle = lv_label_create(boot_splash_screen);
    lv_label_set_text(subtitle, "Inicializando sistema...");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(theme_palette().secondary_text), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 61);

    boot_logo_arc = lv_arc_create(boot_splash_screen);
    lv_obj_set_size(boot_logo_arc, 148, 148);
    lv_arc_set_range(boot_logo_arc, 0, 100);
    lv_arc_set_bg_angles(boot_logo_arc, 0, 360);
    lv_arc_set_rotation(boot_logo_arc, 270);
    lv_arc_set_value(boot_logo_arc, 64);
    lv_obj_set_style_bg_opa(boot_logo_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(boot_logo_arc, lv_color_hex(0xC5DCEB), LV_PART_MAIN);
    lv_obj_set_style_arc_width(boot_logo_arc, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_color(boot_logo_arc, lv_color_hex(BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(boot_logo_arc, 7, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(boot_logo_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(boot_logo_arc, 0, LV_PART_KNOB);
    lv_obj_align(boot_logo_arc, LV_ALIGN_TOP_MID, 0, 102);

    lv_anim_t logo_anim;
    lv_anim_init(&logo_anim);
    lv_anim_set_var(&logo_anim, boot_logo_arc);
    lv_anim_set_values(&logo_anim, 18, 82);
    lv_anim_set_time(&logo_anim, 1500);
    lv_anim_set_playback_time(&logo_anim, 1500);
    lv_anim_set_repeat_count(&logo_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&logo_anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&logo_anim, boot_logo_animation_exec);
    lv_anim_start(&logo_anim);

    lv_obj_t *logo = lv_obj_create(boot_splash_screen);
    lv_obj_remove_style_all(logo);
    lv_obj_set_size(logo, 82, 82);
    lv_obj_set_style_bg_color(logo, lv_color_hex(0xFFF4D8), 0);
    lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(logo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(logo, 2, 0);
    lv_obj_set_style_border_color(logo, lv_color_hex(ORANGE), 0);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 135);

    // A compact school/bank building made from basic LVGL shapes.
    lv_obj_t *roof = lv_obj_create(logo);
    lv_obj_remove_style_all(roof);
    lv_obj_set_size(roof, 52, 8);
    lv_obj_set_style_bg_color(roof, lv_color_hex(BLUE), 0);
    lv_obj_set_style_bg_opa(roof, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(roof, 4, 0);
    lv_obj_align(roof, LV_ALIGN_TOP_MID, 0, 17);
    const lv_coord_t column_x[] = {18, 37, 56};
    for (lv_coord_t x : column_x) {
        lv_obj_t *column = lv_obj_create(logo);
        lv_obj_remove_style_all(column);
        lv_obj_set_size(column, 8, 26);
        lv_obj_set_style_bg_color(column, lv_color_hex(BLUE), 0);
        lv_obj_set_style_bg_opa(column, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(column, 3, 0);
        lv_obj_align(column, LV_ALIGN_TOP_LEFT, x, 29);
    }
    lv_obj_t *base = lv_obj_create(logo);
    lv_obj_remove_style_all(base);
    lv_obj_set_size(base, 52, 7);
    lv_obj_set_style_bg_color(base, lv_color_hex(ORANGE), 0);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(base, 3, 0);
    lv_obj_align(base, LV_ALIGN_TOP_MID, 0, 58);

    boot_progress_bar = lv_bar_create(boot_splash_screen);
    lv_obj_set_size(boot_progress_bar, 600, 24);
    lv_bar_set_range(boot_progress_bar, 0, 100);
    lv_bar_set_value(boot_progress_bar, boot_progress_state.percent, LV_ANIM_OFF);
    boot_bar_current_value = boot_progress_state.percent;
    lv_obj_set_style_bg_color(boot_progress_bar, lv_color_hex(0xC5DCEB), LV_PART_MAIN);
    lv_obj_set_style_bg_color(boot_progress_bar, lv_color_hex(BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_radius(boot_progress_bar, 12, LV_PART_MAIN);
    lv_obj_set_style_radius(boot_progress_bar, 12, LV_PART_INDICATOR);
    lv_obj_align(boot_progress_bar, LV_ALIGN_TOP_MID, 0, 310);

    boot_percent_label = lv_label_create(boot_splash_screen);
    char percent_text[8];
    snprintf(percent_text, sizeof(percent_text), "%u%%",
             static_cast<unsigned>(boot_progress_state.percent));
    lv_label_set_text(boot_percent_label, percent_text);
    lv_obj_set_style_text_font(boot_percent_label, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(boot_percent_label, lv_color_hex(BLUE), 0);
    lv_obj_align(boot_percent_label, LV_ALIGN_TOP_MID, 0, 345);

    boot_state_label = lv_label_create(boot_splash_screen);
    lv_label_set_text(boot_state_label, boot_progress_state.message);
    lv_obj_set_style_text_font(boot_state_label, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(boot_state_label, lv_color_hex(theme_palette().primary_text), 0);
    lv_obj_set_width(boot_state_label, 720);
    lv_label_set_long_mode(boot_state_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(boot_state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(boot_state_label, LV_ALIGN_TOP_MID, 0, 392);

    boot_detail_label = lv_label_create(boot_splash_screen);
    lv_obj_set_style_text_font(boot_detail_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(boot_detail_label, lv_color_hex(theme_palette().secondary_text), 0);
    lv_obj_set_width(boot_detail_label, 760);
    lv_label_set_long_mode(boot_detail_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(boot_detail_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(boot_detail_label, LV_ALIGN_TOP_MID, 0, 431);

    boot_splash_active = true;
    lv_scr_load(boot_splash_screen);
}

void update_boot_detail()
{
    const char *wifi_text = "WiFi: sin conexion";
    switch (wifi_manager.state()) {
        case WiFiState::CONNECTING: wifi_text = "WiFi: conectando"; break;
        case WiFiState::CONNECTED: wifi_text = "WiFi: conectado"; break;
        case WiFiState::ERROR: wifi_text = "WiFi: no disponible"; break;
        case WiFiState::DISCONNECTED: break;
    }
    const char *time_text = time_manager.isSynchronized() ? "Hora sincronizada" : "Hora pendiente";
    const char *sd_text = sd_manager.state() == SdState::SD_NOT_PRESENT ?
                          "microSD: No instalada" : "microSD: no disponible";
    const char *nfc_text = PN532_ENABLED ? "NFC: iniciando" : "NFC: deshabilitado";
    char next_text[sizeof(boot_detail_text)];
    snprintf(next_text, sizeof(next_text), "%s | %s | %s | %s",
             wifi_text, time_text, sd_text, nfc_text);
    if (strcmp(next_text, boot_detail_text) == 0) return;
    strncpy(boot_detail_text, next_text, sizeof(boot_detail_text) - 1);
    boot_detail_text[sizeof(boot_detail_text) - 1] = '\0';
    if (boot_detail_label != nullptr) lv_label_set_text(boot_detail_label, boot_detail_text);
}

void register_student_activity()
{
    last_student_activity_ms = millis();
}

void session_timeout_timer(lv_timer_t *timer)
{
    (void)timer;
    if (pantalla_actual < PANTALLA_ALUMNO || pantalla_actual == PANTALLA_ESPERA ||
        (pantalla_actual >= PANTALLA_CONFIGURACION && pantalla_actual <= PANTALLA_STORAGE_CONFIRMAR_FINAL)) return;
    const uint32_t timeout_ms = SESSION_TIMEOUT_SECONDS * 1000UL;
    if (millis() - last_student_activity_ms >= timeout_ms) {
        transfer_sender_id = 0; transfer_receiver_id = 0; transfer_amount = 0; transfer_receiver_detected = false; transfer_balance_warning = false; transfer_keypad_buffer[0] = '\0';
        mostrar_pantalla(PANTALLA_ESPERA);
    }
}

void nfc_set_state(NfcState state)
{
    portENTER_CRITICAL(&nfc_mux);
    nfc_state = state;
    portEXIT_CRITICAL(&nfc_mux);
}

NfcState nfc_get_state()
{
    portENTER_CRITICAL(&nfc_mux);
    const NfcState state = nfc_state;
    portEXIT_CRITICAL(&nfc_mux);
    return state;
}

uint32_t nfc_get_detection_serial()
{
    portENTER_CRITICAL(&nfc_mux);
    const uint32_t serial = nfc_detection_serial;
    portEXIT_CRITICAL(&nfc_mux);
    return serial;
}

void nfc_copy_uid(const uint8_t *uid, uint8_t uid_length)
{
    portENTER_CRITICAL(&nfc_mux);
    nfc_uid_length = uid_length > sizeof(nfc_uid) ? sizeof(nfc_uid) : uid_length;
    memcpy(nfc_uid, uid, nfc_uid_length);
    ++nfc_detection_serial;
    nfc_state = NFC_CARD_DETECTED;
    portEXIT_CRITICAL(&nfc_mux);
}

void nfc_get_uid(uint8_t *uid, uint8_t *uid_length)
{
    portENTER_CRITICAL(&nfc_mux);
    *uid_length = nfc_uid_length;
    memcpy(uid, nfc_uid, nfc_uid_length);
    portEXIT_CRITICAL(&nfc_mux);
}

void nfc_task(void *parameter)
{
    (void)parameter;
    // The device is constructed only when NFC is enabled. The Adafruit 1.3.4
    // I2C constructor calls pinMode() for IRQ and RESET immediately.
    pn532 = new Adafruit_PN532(-1, -1, &pn532_wire);
    bool initialized = false;
    uint32_t next_init_attempt = 0;
    uint32_t no_card_since = 0;

    for (;;) {
        const uint32_t now = millis();

        if (!initialized) {
            if (now < next_init_attempt) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            pn532_wire.begin(PN532_SDA, PN532_SCL, 100000);
            pn532->begin();
            const uint32_t firmware = pn532->getFirmwareVersion();
            if (firmware == 0 || !pn532->SAMConfig()) {
                nfc_set_state(NFC_NO_DEVICE);
                next_init_attempt = millis() + 2000;
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            // Limita cada sondeo para que la tarea NFC nunca bloquee LVGL.
            pn532->setPassiveActivationRetries(0x01);
            initialized = true;
            nfc_set_state(NFC_READY);
        }

        uint8_t uid[7] = {};
        uint8_t uid_length = 0;
        const bool card_found = pn532->readPassiveTargetID(
            PN532_MIFARE_ISO14443A, uid, &uid_length, 50);

        if (card_found) {
            no_card_since = 0;
            if (!nfc_card_latched) {
                nfc_card_latched = true;
                nfc_copy_uid(uid, uid_length);
            }
        } else if (nfc_card_latched) {
            if (no_card_since == 0) no_card_since = millis();
            if (millis() - no_card_since >= 1000) {
                nfc_card_latched = false;
                nfc_set_state(NFC_READY);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

void set_panel_style(lv_obj_t *object, lv_color_t color, lv_coord_t radius)
{
    lv_obj_set_style_bg_color(object, color, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, radius, 0);
}

lv_obj_t *make_shape(lv_obj_t *parent, lv_coord_t width, lv_coord_t height,
                     uint32_t color, lv_coord_t radius);

void apply_arcade_panel_style(lv_obj_t *object, uint32_t background, lv_coord_t radius,
                              uint32_t accent = 0x000000, bool accented = true)
{
    set_panel_style(object, lv_color_hex(background), radius);
    lv_obj_set_style_border_width(object, accented ? 2 : 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(accented ? accent : theme_palette().border), 0);
    lv_obj_set_style_shadow_width(object, accented ? 8 : 0, 0);
    lv_obj_set_style_shadow_opa(object, current_theme == AppTheme::DARK ? LV_OPA_20 : LV_OPA_10, 0);
}

void apply_arcade_header_style(lv_obj_t *header)
{
    set_panel_style(header, lv_color_hex(theme_palette().surface), 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 2, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(BLUE), 0);
}

void apply_arcade_button_style(lv_obj_t *button, uint32_t background, uint32_t accent, bool enabled = true)
{
    apply_arcade_panel_style(button, enabled ? background : 0xE8F7FF, 15, accent, true);
    lv_obj_set_style_border_width(button, enabled ? 2 : 1, 0);
    lv_obj_set_style_opa(button, enabled ? LV_OPA_COVER : LV_OPA_60, 0);
}

void apply_arcade_badge_style(lv_obj_t *badge, uint32_t background, uint32_t accent)
{
    apply_arcade_panel_style(badge, background, 12, accent, true);
    lv_obj_set_style_shadow_width(badge, 0, 0);
}

void arcade_button_feedback_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *target = lv_event_get_target(event);
    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_transform_zoom(target, 250, 0);
        lv_obj_set_style_opa(target, LV_OPA_90, 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_transform_zoom(target, 256, 0);
        lv_obj_set_style_opa(target, LV_OPA_COVER, 0);
    }
}

lv_obj_t *create_arcade_marker(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                               lv_coord_t width, uint32_t color)
{
    lv_obj_t *marker = make_shape(parent, width, 5, color, 2);
    lv_obj_set_pos(marker, x, y);
    return marker;
}

lv_obj_t *make_shape(lv_obj_t *parent, lv_coord_t width, lv_coord_t height, uint32_t color, lv_coord_t radius = LV_RADIUS_CIRCLE)
{
    lv_obj_t *shape = lv_obj_create(parent);
    lv_obj_remove_style_all(shape);
    lv_obj_set_size(shape, width, height);
    set_panel_style(shape, lv_color_hex(color), radius);
    return shape;
}

void add_header_decorations(lv_obj_t *header)
{
    lv_obj_t *dot = make_shape(header, 13, 13, YELLOW); lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 28, 20);
    dot = make_shape(header, 9, 9, PINK); lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 42, 91);
}

void create_header(lv_obj_t *screen)
{
    lv_obj_t *header = lv_obj_create(screen); lv_obj_remove_style_all(header);
    lv_obj_set_size(header, SCREEN_WIDTH, HEADER_HEIGHT); apply_arcade_header_style(header);
    create_arcade_marker(header, 24, 22, 52, BLUE);

    lv_obj_t *label = lv_label_create(header); lv_label_set_text(label, "BANCO ESCOLAR");
    lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 92, 16);
    label = lv_label_create(header); lv_label_set_text(label, "Pequeñas acciones, grandes logros");
    lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(MUTED), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 92, 54);

    create_date_clock_card(header, 340, 82, 18, 10);
}

lv_obj_t *create_card_icon(lv_obj_t *parent)
{
    lv_obj_t *icon = make_shape(parent, 190, 122, 0xF2F8FF, 22);
    lv_obj_set_style_border_width(icon, 3, 0); lv_obj_set_style_border_color(icon, lv_color_hex(BLUE), 0);
    lv_obj_t *stripe = make_shape(icon, 190, 16, BLUE, 0); lv_obj_align(stripe, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_t *label = lv_label_create(icon); lv_label_set_text(label, "NFC");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_26, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 10);
    lv_obj_t *contact = make_shape(icon, 19, 19, YELLOW); lv_obj_align(contact, LV_ALIGN_BOTTOM_RIGHT, -18, -17);
    return icon;
}

void ring_opacity_anim_exec(void *object, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), static_cast<lv_opa_t>(value), 0);
}

void card_float_anim_exec(void *object, int32_t value) { lv_obj_align(static_cast<lv_obj_t *>(object), LV_ALIGN_CENTER, 0, value); }

void waiting_indicator_anim_exec(void *object, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), static_cast<lv_opa_t>(value), 0);
}

void start_waiting_indicator_animation()
{
    if (!visual_effects_enabled || waiting_indicator == nullptr) return;
    lv_anim_t pulse; lv_anim_init(&pulse); lv_anim_set_var(&pulse, waiting_indicator); lv_anim_set_values(&pulse, 70, LV_OPA_COVER);
    lv_anim_set_time(&pulse, 1200); lv_anim_set_playback_time(&pulse, 1200); lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&pulse, waiting_indicator_anim_exec); lv_anim_start(&pulse);
}

void nfc_panel_pulse_exec(void *object, int32_t value)
{
    lv_obj_set_style_border_opa(static_cast<lv_obj_t *>(object), static_cast<lv_opa_t>(value), 0);
}

void nfc_confirm_exec(void *object, int32_t value)
{
    lv_obj_set_style_border_width(static_cast<lv_obj_t *>(object), value, 0);
}

void start_nfc_confirmation_animation()
{
    if (!visual_effects_enabled || nfc_panel == nullptr) return;
    lv_anim_t confirm; lv_anim_init(&confirm); lv_anim_set_var(&confirm, nfc_panel);
    lv_anim_set_values(&confirm, 2, 8); lv_anim_set_time(&confirm, 180);
    lv_anim_set_playback_time(&confirm, 180); lv_anim_set_exec_cb(&confirm, nfc_confirm_exec);
    lv_anim_start(&confirm);
}

void start_nfc_animations()
{
    if (!visual_effects_enabled) return;

    lv_anim_t outer; lv_anim_init(&outer); lv_anim_set_var(&outer, nfc_ring_outer); lv_anim_set_values(&outer, 28, 120);
    lv_anim_set_time(&outer, 2100); lv_anim_set_playback_time(&outer, 2100); lv_anim_set_delay(&outer, 520);
    lv_anim_set_repeat_count(&outer, LV_ANIM_REPEAT_INFINITE); lv_anim_set_exec_cb(&outer, ring_opacity_anim_exec); lv_anim_start(&outer);
    lv_anim_t middle; lv_anim_init(&middle); lv_anim_set_var(&middle, nfc_ring_middle); lv_anim_set_values(&middle, 38, 145);
    lv_anim_set_time(&middle, 1900); lv_anim_set_playback_time(&middle, 1900); lv_anim_set_delay(&middle, 260);
    lv_anim_set_repeat_count(&middle, LV_ANIM_REPEAT_INFINITE); lv_anim_set_exec_cb(&middle, ring_opacity_anim_exec); lv_anim_start(&middle);
    lv_anim_t inner; lv_anim_init(&inner); lv_anim_set_var(&inner, nfc_ring_inner); lv_anim_set_values(&inner, 52, 175);
    lv_anim_set_time(&inner, 1700); lv_anim_set_playback_time(&inner, 1700);
    lv_anim_set_repeat_count(&inner, LV_ANIM_REPEAT_INFINITE); lv_anim_set_exec_cb(&inner, ring_opacity_anim_exec); lv_anim_start(&inner);
    lv_anim_t card; lv_anim_init(&card); lv_anim_set_var(&card, nfc_card_visual); lv_anim_set_values(&card, 0, -6);
    lv_anim_set_time(&card, 1800); lv_anim_set_playback_time(&card, 1800); lv_anim_set_repeat_count(&card, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&card, card_float_anim_exec); lv_anim_start(&card);

    lv_anim_t panel; lv_anim_init(&panel); lv_anim_set_var(&panel, nfc_panel); lv_anim_set_values(&panel, 100, 220);
    lv_anim_set_time(&panel, 2600); lv_anim_set_playback_time(&panel, 2600); lv_anim_set_repeat_count(&panel, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&panel, nfc_panel_pulse_exec); lv_anim_start(&panel);
}

void nfc_area_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (DEMO_MODE) {
        register_student_activity();
        mostrar_pantalla(PANTALLA_DETECCION);
        return;
    }
    start_nfc_confirmation_animation();
}

void demo_entry_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    selected_student = getDemoStudent();
    if (selected_student == nullptr) return;
    register_student_activity();
    mostrar_pantalla(PANTALLA_ALUMNO);
}

void format_uid(const uint8_t *uid, uint8_t uid_length, char *buffer, size_t buffer_size)
{
    size_t offset = 0;
    for (uint8_t i = 0; i < uid_length && offset + 4 < buffer_size; ++i) {
        const int written = snprintf(buffer + offset, buffer_size - offset,
                                     i == 0 ? "%02X" : ":%02X", uid[i]);
        if (written < 0) break;
        offset += static_cast<size_t>(written);
    }
    if (offset < buffer_size) buffer[offset] = '\0';
}

void update_nfc_ui_timer(lv_timer_t *timer)
{
    (void)timer;
    if (pantalla_actual != PANTALLA_ESPERA || nfc_message_label == nullptr || nfc_hint_label == nullptr) return;
    static NfcState previous_state = NFC_INITIALIZING;
    static uint32_t previous_serial = 0;
    static uint32_t show_uid_until = 0;
    const NfcState state = nfc_get_state();
    const uint32_t serial = nfc_get_detection_serial();
    const uint32_t now = millis();

    if (serial != previous_serial) {
        uint8_t uid[7] = {};
        uint8_t uid_length = 0;
        nfc_get_uid(uid, &uid_length);
        char uid_text[32] = "UID: ";
        format_uid(uid, uid_length, uid_text + 5, sizeof(uid_text) - 5);
        lv_label_set_text(nfc_message_label, "Tarjeta detectada");
        lv_label_set_text(nfc_hint_label, uid_text);
        if (nfc_status_label) lv_label_set_text(nfc_status_label, "NFC listo");
        start_nfc_confirmation_animation();
        show_uid_until = now + 2200;
        previous_serial = serial;
    }

    if (state != previous_state && !(state == NFC_CARD_DETECTED && now < show_uid_until)) {
        if (state == NFC_INITIALIZING) {
            lv_label_set_text(nfc_message_label, "Inicializando NFC...");
            lv_label_set_text(nfc_hint_label, "Preparando el lector");
            if (nfc_status_label) lv_label_set_text(nfc_status_label, "NFC pendiente");
        } else if (state == NFC_NO_DEVICE) {
            lv_label_set_text(nfc_message_label, "NFC no encontrado");
            lv_label_set_text(nfc_hint_label, "Revisa la conexión del lector");
            if (nfc_status_label) lv_label_set_text(nfc_status_label, "NFC pendiente");
        } else if (state == NFC_READY) {
            lv_label_set_text(nfc_message_label, "Acerca tu tarjeta");
            lv_label_set_text(nfc_hint_label, "Identif\xC3\xAD" "cate para continuar");
            if (nfc_status_label) lv_label_set_text(nfc_status_label, "NFC listo");
        }
        previous_state = state;
    }

    if (show_uid_until != 0 && now >= show_uid_until && state == NFC_CARD_DETECTED) {
        lv_label_set_text(nfc_message_label, "Acerca tu tarjeta");
        lv_label_set_text(nfc_hint_label, "Identif\xC3\xAD" "cate para continuar");
        show_uid_until = 0;
    }
}

void update_clock_timer(lv_timer_t *timer)
{
    (void)timer;
    char date_text[48];
    char time_text[16];
    obtener_texto_fecha_hora(date_text, sizeof(date_text), time_text, sizeof(time_text));
    if (clock_label) lv_label_set_text(clock_label, time_text);
    if (date_label) lv_label_set_text(date_label, date_text);
}

const char *wifi_state_text(WiFiState state)
{
    switch (state) {
        case WiFiState::CONNECTED: return "Wi-Fi conectado";
        case WiFiState::CONNECTING: return "Conectando...";
        case WiFiState::DISCONNECTED: return "Wi-Fi sin conexi\xC3\xB3n";
        case WiFiState::ERROR: return "Error de conexi\xC3\xB3n";
    }
    return "Wi-Fi sin conexi\xC3\xB3n";
}

void update_wifi_ui()
{
    static WiFiSnapshot previous = {};
    const WiFiSnapshot current = wifi_manager.snapshot();
    if (memcmp(&current, &previous, sizeof(current)) == 0 && !wifi_ui_needs_refresh) return;
    previous = current;
    wifi_ui_needs_refresh = false;
    if (wifi_status_label) lv_label_set_text(wifi_status_label, wifi_state_text(current.state));
    if (!wifi_config_state_label && !wifi_config_ssid_label && !wifi_config_ip_label && !wifi_config_rssi_label) return;
    if (wifi_config_state_label) {
        const char *state_text = current.connected ? "Conectado" :
            (current.state == WiFiState::CONNECTING ? "Conectando..." :
             (current.state == WiFiState::ERROR ? "Error de conexi\xC3\xB3n" : "Sin conexi\xC3\xB3n"));
        lv_label_set_text(wifi_config_state_label, state_text);
    }
    if (wifi_config_ssid_label) lv_label_set_text(wifi_config_ssid_label, current.state == WiFiState::DISCONNECTED ? "--" : (current.ssid[0] ? current.ssid : "--"));
    if (wifi_config_ip_label) lv_label_set_text(wifi_config_ip_label, current.connected ? current.ip : "--");
    if (wifi_config_rssi_label) {
        if (current.connected) { char text[24]; snprintf(text, sizeof(text), "%ld dBm", static_cast<long>(current.rssi)); lv_label_set_text(wifi_config_rssi_label, text); }
        else lv_label_set_text(wifi_config_rssi_label, "--");
    }
}

static const char *const motivation_messages[] = {
    "\xC2\xA1Tu esfuerzo cuenta!",
    "Cada \xC3\x81ureo te acerca a una meta",
    "Participa, ahorra y alcanza tus metas",
    "Peque\xC3\xB1" "as acciones crean grandes logros"
};

void motivation_fade_exec(void *object, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), static_cast<lv_opa_t>(value), 0);
}

void motivation_fade_in(lv_obj_t *label)
{
    lv_anim_t fade_in; lv_anim_init(&fade_in); lv_anim_set_var(&fade_in, label); lv_anim_set_values(&fade_in, 0, LV_OPA_COVER);
    lv_anim_set_time(&fade_in, 260); lv_anim_set_exec_cb(&fade_in, motivation_fade_exec); lv_anim_start(&fade_in);
}

void motivation_fade_out_ready(lv_anim_t *anim)
{
    (void)anim;
    motivation_index = (motivation_index + 1) % 4;
    lv_label_set_text(motivation_label, motivation_messages[motivation_index]);
    motivation_fade_in(motivation_label);
}

void motivation_timer(lv_timer_t *timer)
{
    (void)timer;
}

uint8_t particle_intensity_for_screen(Pantalla pantalla)
{
    switch (pantalla) {
        case PANTALLA_ESPERA: return 8;
        case PANTALLA_ALUMNO: return 8;
        case PANTALLA_DETECCION: return 3;
        case PANTALLA_CUENTA:
        case PANTALLA_PROGRESO:
        case PANTALLA_METAS:
        case PANTALLA_LOGROS: return 5;
        case PANTALLA_TRANSFERIR: return 3;
        case PANTALLA_TRANSFER_TECLADO:
        case PANTALLA_TRANSFER_CONFIRMAR: return 2;
        case PANTALLA_TRANSFER_RESULTADO: return 3;
        case PANTALLA_CONFIGURACION: return 3;
        case PANTALLA_FLUIDEZ:
        case PANTALLA_DICTADO: return 3;
        case PANTALLA_ENTRADA: return 3;
    }
    return 0;
}

void particle_y_exec(void *object, int32_t value)
{
    lv_obj_set_y(static_cast<lv_obj_t *>(object), value);
}

void create_particle_background(lv_obj_t *screen, uint8_t intensity)
{
    if (!visual_effects_enabled || intensity == 0) return;

    static const lv_coord_t x_positions[] = {34, 96, 168, 244, 326, 404, 486, 566, 644, 714, 758, 282};
    static const lv_coord_t y_positions[] = {438, 392, 348, 420, 300, 454, 368, 418, 334, 452, 386, 270};
    static const uint8_t sizes[] = {4, 3, 5, 3, 4, 6, 3, 5, 4, 3, 5, 4};
    static const uint32_t colors[] = {BLUE, PURPLE, GREEN, YELLOW, ORANGE, BLUE, PINK, GREEN, PURPLE, YELLOW, BLUE, ORANGE};
    static const uint32_t durations[] = {5600, 6200, 6800, 5900, 7200, 6400, 7600, 6100, 7000, 6600, 7400, 6300};

    const uint8_t count = intensity > 12 ? 12 : intensity;
    for (uint8_t i = 0; i < count; ++i) {
        lv_obj_t *particle = make_shape(screen, sizes[i], sizes[i], colors[i]);
        lv_obj_set_pos(particle, x_positions[i], y_positions[i]);
        lv_obj_set_style_opa(particle, current_theme == AppTheme::DARK ? LV_OPA_40 : LV_OPA_60, 0);
        lv_obj_clear_flag(particle, LV_OBJ_FLAG_CLICKABLE);

        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, particle);
        lv_anim_set_values(&anim, y_positions[i], -12);
        lv_anim_set_time(&anim, durations[i]);
        lv_anim_set_delay(&anim, i * 230);
        lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&anim, particle_y_exec);
        lv_anim_start(&anim);
    }
}

void create_performance_overlay(lv_obj_t *screen)
{
    if (!performance_monitor_enabled) return;

    lv_coord_t x = MONITOR_SAFE_X;
    lv_coord_t y = MONITOR_SAFE_Y;
    if (pantalla_actual == PANTALLA_ESPERA) {
        // Mantiene el monitor por encima de la barra inferior, sin tapar sus estados.
        x = WAIT_MONITOR_SAFE_X;
        y = WAIT_MONITOR_SAFE_Y;
    } else if (pantalla_actual == PANTALLA_ALUMNO) {
        // En Inicio queda a la derecha de las tarjetas de navegación.
        x = 650;
        y = 330;
    }

    performance_overlay = lv_obj_create(screen);
    lv_obj_remove_style_all(performance_overlay);
    lv_obj_set_size(performance_overlay, MONITOR_WIDTH, MONITOR_HEIGHT);
    lv_obj_set_pos(performance_overlay, x, y);
    set_panel_style(performance_overlay, lv_color_hex(0xE8F0FF), 12);
    lv_obj_set_style_border_width(performance_overlay, 1, 0);
    lv_obj_set_style_border_color(performance_overlay, lv_color_hex(BLUE), 0);
    lv_obj_set_style_bg_opa(performance_overlay, LV_OPA_90, 0);
    lv_obj_clear_flag(performance_overlay, LV_OBJ_FLAG_CLICKABLE);

    performance_label = lv_label_create(performance_overlay);
    lv_obj_set_width(performance_label, 104);
    lv_obj_set_height(performance_label, 50);
    lv_obj_set_style_text_font(performance_label, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(performance_label, lv_color_hex(INK), 0);
    lv_label_set_long_mode(performance_label, LV_LABEL_LONG_CLIP);
    lv_obj_align(performance_label, LV_ALIGN_CENTER, 0, 0);
    performance_last_frame_count = esp_lv_adapter_get_frame_count();
    performance_last_sample_ms = millis();
}

void update_performance_overlay(lv_timer_t *timer)
{
    (void)timer;
    const uint32_t now = millis();
    const uint32_t elapsed = now - performance_last_sample_ms;
    if (elapsed == 0) return;

    const uint32_t frame_count = esp_lv_adapter_get_frame_count();
    const uint32_t frames = frame_count - performance_last_frame_count;
    const uint32_t fps = (frames * 1000UL + elapsed / 2UL) / elapsed;
    const uint32_t free_heap = ESP.getFreeHeap();
    const uint32_t total_heap = ESP.getHeapSize();
    const uint32_t min_heap = ESP.getMinFreeHeap();
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const bool sample_metrics = PERFORMANCE_TEST_ENABLED || performance_monitor_enabled;
    if (sample_metrics) {
        PerformanceScreenStats &stats = performance_stats[static_cast<uint8_t>(pantalla_actual)];
        ++stats.samples;
        if (fps < stats.fps_min) stats.fps_min = fps;
        if (fps > stats.fps_max) stats.fps_max = fps;
        stats.fps_sum += fps;
        if (free_heap < stats.heap_min) stats.heap_min = free_heap;
        if (psram_free < stats.psram_free_min) stats.psram_free_min = psram_free;
        if (PERFORMANCE_TEST_ENABLED) {
            uint16_t timer_count = 0;
            for (lv_timer_t *active_timer = lv_timer_get_next(nullptr); active_timer != nullptr;
                 active_timer = lv_timer_get_next(active_timer)) ++timer_count;
            Serial.printf("[Perf] Screen=%s FPS=%lu HeapFree=%luKB HeapUsed=%luKB MinHeap=%luKB PSRAMFree=%luKB PSRAMUsed=%luKB PSRAMTotal=%luKB Anim=%u Timers=%u WiFi=%u NTP=%u Storage=%u\n",
                          performance_screen_name(pantalla_actual), static_cast<unsigned long>(fps),
                          static_cast<unsigned long>(free_heap / 1024U),
                          static_cast<unsigned long>((total_heap - free_heap) / 1024U),
                          static_cast<unsigned long>(min_heap / 1024U),
                          static_cast<unsigned long>(psram_free / 1024U),
                          static_cast<unsigned long>((psram_total - psram_free) / 1024U),
                          static_cast<unsigned long>(psram_total / 1024U),
                          static_cast<unsigned>(lv_anim_count_running()), static_cast<unsigned>(timer_count),
                          static_cast<unsigned>(wifi_manager.state()),
                          static_cast<unsigned>(time_manager.state()),
                          static_cast<unsigned>(storage_manager.state()));
        }
    }

    if (!performance_monitor_enabled || performance_label == nullptr) {
        performance_last_frame_count = frame_count;
        performance_last_sample_ms = now;
        return;
    }

    char text[96];
    snprintf(text, sizeof(text), "FPS %lu\nRAM %luK\nPSR %luK",
             static_cast<unsigned long>(fps),
             static_cast<unsigned long>(free_heap / 1024U),
             static_cast<unsigned long>(psram_free / 1024U));
    lv_label_set_text(performance_label, text);
    performance_last_frame_count = frame_count;
    performance_last_sample_ms = now;
}

const char *performance_screen_name(Pantalla screen)
{
    switch (screen) {
        case PANTALLA_ESPERA: return "Home";
        case PANTALLA_DETECCION: return "Demo";
        case PANTALLA_ALUMNO: return "StudentHome";
        case PANTALLA_CUENTA: return "Account";
        case PANTALLA_PROGRESO: return "Progress";
        case PANTALLA_FLUIDEZ: return "ReadingChart";
        case PANTALLA_DICTADO: return "Dictation";
        case PANTALLA_METAS: return "Goals";
        case PANTALLA_LOGROS: return "Achievements";
        case PANTALLA_ENTRADA: return "Entry";
        case PANTALLA_CONFIGURACION: return "Config";
        case PANTALLA_WIFI_REDES: return "WiFiNetworks";
        case PANTALLA_WIFI_PASSWORD: return "WiFiPassword";
        case PANTALLA_WIFI_PROGRESS: return "WiFiProgress";
        case PANTALLA_WIFI_RESULT: return "WiFiResult";
        case PANTALLA_WIFI_OLVIDAR: return "WiFiForget";
        case PANTALLA_STORAGE_LOCAL: return "LocalStorage";
        case PANTALLA_STORAGE_CONFIRMAR: return "StorageConfirm";
        case PANTALLA_STORAGE_CONFIRMAR_FINAL: return "StorageConfirmFinal";
        case PANTALLA_TRANSFERIR: return "Transfer";
        case PANTALLA_TRANSFER_TECLADO: return "TransferKeypad";
        case PANTALLA_TRANSFER_CONFIRMAR: return "TransferConfirm";
        case PANTALLA_TRANSFER_RESULTADO: return "TransferResult";
    }
    return "Unknown";
}

void performance_test_step_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!PERFORMANCE_TEST_ENABLED || transition_in_progress) return;
    static const Pantalla route[] = {
        PANTALLA_ESPERA, PANTALLA_ALUMNO, PANTALLA_CUENTA, PANTALLA_PROGRESO,
        PANTALLA_FLUIDEZ, PANTALLA_DICTADO, PANTALLA_TRANSFERIR,
        PANTALLA_CONFIGURACION, PANTALLA_WIFI_REDES, PANTALLA_STORAGE_LOCAL,
        PANTALLA_ESPERA
    };
    static const bool effects[] = {true, true, false, false};
    static const bool monitor[] = {false, true, false, true};
    if (performance_test_step == 0) {
        visual_effects_enabled = effects[performance_test_scenario];
        performance_monitor_enabled = monitor[performance_test_scenario];
        Serial.printf("[Perf] Scenario %u Effects=%s Monitor=%s\n",
                      static_cast<unsigned>(performance_test_scenario + 1),
                      visual_effects_enabled ? "ON" : "OFF",
                      performance_monitor_enabled ? "ON" : "OFF");
        performance_test_step = 1;
        mostrar_pantalla(route[0]);
        lv_timer_set_period(timer, PERFORMANCE_SCREEN_HOLD_MS);
        return;
    }
    const size_t route_length = sizeof(route) / sizeof(route[0]);
    if (performance_test_step >= route_length) {
        Serial.printf("[Perf] ===== SUMMARY Scenario %u =====\n",
                      static_cast<unsigned>(performance_test_scenario + 1));
        for (uint8_t i = 0; i < sizeof(route) / sizeof(route[0]); ++i) {
            const Pantalla screen = route[i];
            PerformanceScreenStats &stats = performance_stats[static_cast<uint8_t>(screen)];
            if (stats.samples == 0) continue;
            const uint32_t average_fps = static_cast<uint32_t>(stats.fps_sum / stats.samples);
            const char *rating = average_fps >= 50 ? "very-fluid" :
                                 average_fps >= 40 ? "acceptable" :
                                 average_fps >= 30 ? "perceptible" : "slow";
            Serial.printf("[Perf] %s FPS min/avg/max=%lu/%lu/%lu (%s) HeapMin=%luKB PSRAMMin=%luKB Create=%lums Transition=%lums\n",
                          performance_screen_name(screen), static_cast<unsigned long>(stats.fps_min),
                          static_cast<unsigned long>(average_fps),
                          static_cast<unsigned long>(stats.fps_max),
                          rating,
                          static_cast<unsigned long>(stats.heap_min / 1024U),
                          static_cast<unsigned long>(stats.psram_free_min / 1024U),
                          static_cast<unsigned long>(stats.create_ms),
                          static_cast<unsigned long>(stats.transition_ms));
            stats = PerformanceScreenStats{};
        }
        ++performance_test_scenario;
        performance_test_step = 0;
        if (performance_test_scenario >= 4) {
            visual_effects_enabled = true;
            performance_monitor_enabled = false;
            performance_test_timer = nullptr;
            lv_timer_del(timer);
            Serial.println("[Perf] Suite complete; restored Effects=ON Monitor=OFF");
            return;
        }
    } else {
        mostrar_pantalla(route[performance_test_step]);
        ++performance_test_step;
    }
    lv_timer_set_period(timer, PERFORMANCE_SCREEN_HOLD_MS);
}

void create_nfc_area(lv_obj_t *screen)
{
    lv_obj_t *card = lv_obj_create(screen); lv_obj_remove_style_all(card); lv_obj_set_size(card, 764, 266);
    nfc_panel = card;
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 118); apply_arcade_panel_style(card, 0xFFFFFF, 24, BLUE);
    lv_obj_t *visual_column = lv_obj_create(card); lv_obj_remove_style_all(visual_column); lv_obj_set_size(visual_column, 318, 238); lv_obj_align(visual_column, LV_ALIGN_LEFT_MID, 16, 0);
    nfc_ring_outer = make_shape(visual_column, 212, 212, SKY); lv_obj_set_style_bg_opa(nfc_ring_outer, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(nfc_ring_outer, 4, 0); lv_obj_set_style_border_color(nfc_ring_outer, lv_color_hex(0x8FD8F2), 0); lv_obj_align(nfc_ring_outer, LV_ALIGN_CENTER, 0, 0);
    nfc_ring_middle = make_shape(visual_column, 198, 198, SKY); lv_obj_set_style_bg_opa(nfc_ring_middle, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(nfc_ring_middle, 3, 0); lv_obj_set_style_border_color(nfc_ring_middle, lv_color_hex(GREEN), 0); lv_obj_align(nfc_ring_middle, LV_ALIGN_CENTER, 0, 0);
    nfc_ring_inner = make_shape(visual_column, 184, 184, SKY); lv_obj_set_style_bg_opa(nfc_ring_inner, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(nfc_ring_inner, 3, 0); lv_obj_set_style_border_color(nfc_ring_inner, lv_color_hex(BLUE), 0); lv_obj_align(nfc_ring_inner, LV_ALIGN_CENTER, 0, 0);
    nfc_card_visual = create_card_icon(visual_column); lv_obj_align(nfc_card_visual, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *info = lv_obj_create(card); lv_obj_remove_style_all(info); lv_obj_set_size(info, 410, 238); lv_obj_align(info, LV_ALIGN_RIGHT_MID, -16, 0); apply_arcade_panel_style(info, 0xF7FBFF, 18, PURPLE, false);
    nfc_message_label = lv_label_create(info); lv_label_set_text(nfc_message_label, "Acerca tu tarjeta"); lv_obj_set_style_text_font(nfc_message_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(nfc_message_label, lv_color_hex(INK), 0); lv_obj_set_width(nfc_message_label, 380); lv_obj_set_style_text_align(nfc_message_label, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(nfc_message_label, LV_ALIGN_TOP_MID, 0, 36);
    nfc_hint_label = lv_label_create(info); lv_label_set_text(nfc_hint_label, "Identif\xC3\xAD" "cate para continuar"); lv_obj_set_style_text_font(nfc_hint_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(nfc_hint_label, lv_color_hex(MUTED), 0); lv_obj_set_width(nfc_hint_label, 380); lv_obj_set_style_text_align(nfc_hint_label, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(nfc_hint_label, LV_ALIGN_TOP_MID, 0, 82);
    waiting_indicator = make_shape(info, 12, 12, PURPLE); lv_obj_align(waiting_indicator, LV_ALIGN_TOP_MID, -152, 145);
    motivation_label = lv_label_create(info); lv_label_set_text(motivation_label, "Esperando tarjeta..."); lv_obj_set_style_text_font(motivation_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(motivation_label, lv_color_hex(PURPLE), 0); lv_obj_set_width(motivation_label, 300); lv_obj_set_style_text_align(motivation_label, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(motivation_label, LV_ALIGN_TOP_MID, 10, 139);
    lv_obj_t *demo_button = lv_obj_create(info); lv_obj_remove_style_all(demo_button); lv_obj_set_size(demo_button, 220, 40); lv_obj_align(demo_button, LV_ALIGN_TOP_MID, 0, 186);
    apply_arcade_button_style(demo_button, 0xE8F0FF, BLUE);
    lv_obj_add_flag(demo_button, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(demo_button, arcade_button_feedback_event, LV_EVENT_ALL, nullptr); lv_obj_add_event_cb(demo_button, demo_entry_event, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *demo_label = lv_label_create(demo_button); lv_label_set_text(demo_label, "ENTRAR A DEMO"); lv_obj_set_style_text_font(demo_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(demo_label, lv_color_hex(INK), 0); lv_obj_center(demo_label);
    if (visual_effects_enabled) {
        start_waiting_indicator_animation();
        start_nfc_animations();
    }
}

void create_status_chip(lv_obj_t *parent, lv_coord_t x, lv_coord_t width, uint32_t bg, uint32_t dot_color, const char *text, lv_obj_t **label_out = nullptr)
{
    lv_obj_t *chip = lv_obj_create(parent); lv_obj_remove_style_all(chip); lv_obj_set_size(chip, width, 44); apply_arcade_badge_style(chip, bg, dot_color); lv_obj_align(chip, LV_ALIGN_LEFT_MID, x, 0);
    lv_obj_t *dot = make_shape(chip, 12, 12, dot_color); lv_obj_align(dot, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t *label = lv_label_create(chip); lv_label_set_text(label, text); lv_obj_set_width(label, width - 48); lv_obj_set_height(label, 24); lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0); lv_obj_align(label, LV_ALIGN_LEFT_MID, 36, 0);
    if (label_out) *label_out = label;
}

void draw_config_gear(lv_obj_t *canvas)
{
    lv_canvas_set_buffer(canvas, config_gear_canvas_buffer, CONFIG_GEAR_SIZE,
                         CONFIG_GEAR_SIZE, LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED);
    lv_canvas_fill_bg(canvas, lv_color_hex(0x00FF00), LV_OPA_COVER);

    lv_draw_rect_dsc_t gear_part;
    lv_draw_rect_dsc_init(&gear_part);
    gear_part.bg_color = lv_color_hex(BLUE);
    gear_part.bg_opa = LV_OPA_COVER;
    gear_part.border_width = 0;
    gear_part.radius = 2;
    lv_canvas_draw_rect(canvas, 12, 1, 8, 8, &gear_part);
    lv_canvas_draw_rect(canvas, 12, 23, 8, 8, &gear_part);
    lv_canvas_draw_rect(canvas, 1, 12, 8, 8, &gear_part);
    lv_canvas_draw_rect(canvas, 23, 12, 8, 8, &gear_part);
    lv_canvas_draw_rect(canvas, 4, 4, 7, 7, &gear_part);
    lv_canvas_draw_rect(canvas, 21, 4, 7, 7, &gear_part);
    lv_canvas_draw_rect(canvas, 4, 21, 7, 7, &gear_part);
    lv_canvas_draw_rect(canvas, 21, 21, 7, 7, &gear_part);

    gear_part.radius = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(canvas, 6, 6, 20, 20, &gear_part);

    gear_part.bg_color = lv_color_hex(0xFBEAF2);
    lv_canvas_draw_rect(canvas, 12, 12, 8, 8, &gear_part);
}

void config_gear_rotation_exec(void *object, int32_t angle)
{
    config_gear_angle = angle % 3600;
    if (config_gear_angle < 0) config_gear_angle += 3600;
    lv_obj_set_style_transform_angle(static_cast<lv_obj_t *>(object), config_gear_angle, 0);
}

void create_status_bar(lv_obj_t *screen)
{
    lv_obj_t *bar = lv_obj_create(screen); lv_obj_remove_style_all(bar); lv_obj_set_size(bar, 764, 56); lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -18); apply_arcade_panel_style(bar, 0xF4F8FC, 16, BLUE, false);
    create_status_chip(bar, 12, 172, 0xE4F7EC, GREEN, "Sistema listo");
    create_status_chip(bar, 204, 190, 0xFFF4D8, YELLOW, "NFC pendiente", &nfc_status_label);
    create_status_chip(bar, 414, 226, 0xE8F0FF, BLUE, "Wi-Fi sin conexi\xC3\xB3n", &wifi_status_label);
    lv_obj_t *settings = lv_obj_create(bar); lv_obj_remove_style_all(settings); lv_obj_set_size(settings, 72, 44); lv_obj_set_pos(settings, 668, 6); apply_arcade_button_style(settings, 0xFBEAF2, PINK); lv_obj_add_flag(settings, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(settings, arcade_button_feedback_event, LV_EVENT_ALL, nullptr); lv_obj_add_event_cb(settings, config_event, LV_EVENT_CLICKED, nullptr);
    config_gear_animating = false;
    config_gear_canvas = lv_canvas_create(settings);
    lv_obj_set_size(config_gear_canvas, CONFIG_GEAR_SIZE, CONFIG_GEAR_SIZE);
    lv_obj_set_style_transform_pivot_x(config_gear_canvas, CONFIG_GEAR_SIZE / 2, 0);
    lv_obj_set_style_transform_pivot_y(config_gear_canvas, CONFIG_GEAR_SIZE / 2, 0);
    lv_obj_set_style_transform_angle(config_gear_canvas, 0, 0);
    config_gear_angle = 0;
    draw_config_gear(config_gear_canvas);
    lv_obj_align(config_gear_canvas, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(config_gear_canvas, LV_OBJ_FLAG_CLICKABLE);
    if (visual_effects_enabled) {
        lv_anim_t idle_rotation;
        lv_anim_init(&idle_rotation);
        lv_anim_set_var(&idle_rotation, config_gear_canvas);
        lv_anim_set_values(&idle_rotation, 0, 3600);
        lv_anim_set_time(&idle_rotation, CONFIG_GEAR_IDLE_ROTATION_MS);
        lv_anim_set_repeat_count(&idle_rotation, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&idle_rotation, lv_anim_path_linear);
        lv_anim_set_exec_cb(&idle_rotation, config_gear_rotation_exec);
        lv_anim_start(&idle_rotation);
    }
}

void create_date_clock_card(lv_obj_t *parent, lv_coord_t width, lv_coord_t height,
                            lv_coord_t right_offset, lv_coord_t top_offset)
{
    lv_obj_t *date_card = lv_obj_create(parent); lv_obj_remove_style_all(date_card);
    lv_obj_set_size(date_card, width, height); apply_arcade_badge_style(date_card, 0xE8F7FF, BLUE);
    lv_obj_align(date_card, LV_ALIGN_TOP_RIGHT, right_offset, top_offset);
    char date_text[48];
    char time_text[16];
    obtener_texto_fecha_hora(date_text, sizeof(date_text), time_text, sizeof(time_text));
    date_label = lv_label_create(date_card); lv_label_set_text(date_label, date_text);
    lv_obj_set_style_text_font(date_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(date_label, lv_color_hex(INK), 0);
    lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_width(date_label, width - 20); lv_obj_set_height(date_label, 38); lv_label_set_long_mode(date_label, LV_LABEL_LONG_WRAP); lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 4);
    clock_label = lv_label_create(date_card); lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_26, 0);
    lv_label_set_text(clock_label, time_text);
    lv_obj_set_width(clock_label, width - 12); lv_label_set_long_mode(clock_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(clock_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(clock_label, lv_color_hex(PURPLE), 0); lv_obj_align(clock_label, LV_ALIGN_BOTTOM_MID, 0, -6);
}

void create_student_topbar(lv_obj_t *screen, const char *title, bool show_exit, bool show_profile)
{
    (void)show_profile;
    lv_obj_t *header = lv_obj_create(screen); lv_obj_remove_style_all(header);
    lv_obj_set_size(header, SCREEN_WIDTH, HEADER_HEIGHT); apply_arcade_header_style(header);
    create_arcade_marker(header, 24, 22, 52, ORANGE);
    lv_obj_t *brand = lv_label_create(header); lv_label_set_text(brand, "BANCO ESCOLAR");
    lv_obj_set_style_text_font(brand, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(brand, lv_color_hex(BLUE), 0);
    lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 92, 12);
    lv_obj_t *label = lv_label_create(header); lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0);
    lv_obj_set_width(label, show_exit ? 300 : 370); lv_obj_align(label, LV_ALIGN_TOP_LEFT, 92, 52);

    if (show_exit) {
        lv_obj_t *exit = lv_obj_create(header); lv_obj_remove_style_all(exit); lv_obj_set_size(exit, 64, 42);
        apply_arcade_button_style(exit, 0xFBEAF2, PINK); lv_obj_align(exit, LV_ALIGN_TOP_LEFT, 414, 27);
        lv_obj_t *dot = make_shape(exit, 11, 11, PINK); lv_obj_align(dot, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_t *exit_label = lv_label_create(exit); lv_label_set_text(exit_label, "Salir"); lv_obj_set_style_text_font(exit_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(exit_label, lv_color_hex(INK), 0); lv_obj_align(exit_label, LV_ALIGN_LEFT_MID, 24, 0);
        lv_obj_add_flag(exit, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(exit, arcade_button_feedback_event, LV_EVENT_ALL, nullptr); lv_obj_add_event_cb(exit, demo_exit_event, LV_EVENT_CLICKED, nullptr);
    }
    create_date_clock_card(header, show_exit ? 288 : 300, 82, 18, 8);
}

void create_demo_avatar(lv_obj_t *parent)
{
    lv_obj_t *avatar = make_shape(parent, 86, 86, SKY); lv_obj_align(avatar, LV_ALIGN_TOP_LEFT, 42, 124);
    lv_obj_t *face = make_shape(avatar, 58, 58, YELLOW); lv_obj_align(face, LV_ALIGN_CENTER, 0, 6);
    lv_obj_t *hair = make_shape(avatar, 56, 22, ORANGE, 11); lv_obj_align(hair, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_t *eye = make_shape(face, 7, 7, INK); lv_obj_align(eye, LV_ALIGN_TOP_LEFT, 17, 25);
    eye = make_shape(face, 7, 7, INK); lv_obj_align(eye, LV_ALIGN_TOP_RIGHT, -17, 25);
    lv_obj_t *smile = make_shape(face, 22, 5, PINK, 3); lv_obj_align(smile, LV_ALIGN_BOTTOM_MID, 0, -13);
}

void balance_appear_exec(void *object, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), static_cast<lv_opa_t>(value), 0);
    lv_obj_set_y(static_cast<lv_obj_t *>(object), 112 + (255 - value) / 18);
}

void animate_balance_card(lv_obj_t *balance)
{
    if (!visual_effects_enabled) {
        lv_obj_set_style_opa(balance, LV_OPA_COVER, 0);
        lv_obj_set_y(balance, 24);
        return;
    }
    lv_anim_t appear; lv_anim_init(&appear); lv_anim_set_var(&appear, balance); lv_anim_set_values(&appear, 0, LV_OPA_COVER);
    lv_anim_set_time(&appear, 520); lv_anim_set_exec_cb(&appear, balance_appear_exec); lv_anim_start(&appear);
}

void demo_menu_event(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target(event);
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_transform_zoom(button, 246, 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_transform_zoom(button, 256, 0);
    } else if (code == LV_EVENT_CLICKED) {
        register_student_activity();
        const uintptr_t target = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
        if (static_cast<Pantalla>(target) == PANTALLA_TRANSFERIR) {
            transfer_sender_id = selected_student ? selected_student->student_id : 0;
            transfer_receiver_id = 0; transfer_amount = 0; transfer_receiver_detected = false; transfer_balance_warning = false; transfer_keypad_buffer[0] = '\0';
        }
        mostrar_pantalla(static_cast<Pantalla>(target));
    }
}

void create_student_menu_button_sized(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height,
                                      uint32_t color, uint32_t icon_color, const char *title, const char *description, Pantalla target)
{
    lv_obj_t *button = lv_obj_create(parent); lv_obj_remove_style_all(button); lv_obj_set_size(button, width, height);
    apply_arcade_button_style(button, color, icon_color); lv_obj_align(button, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_t *icon = make_shape(button, 38, 38, icon_color); lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 16, (height - 38) / 2);
    lv_obj_t *icon_dot = make_shape(icon, 12, 12, 0xFFFFFF); lv_obj_align(icon_dot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *label = lv_label_create(button); lv_label_set_text(label, title); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0); lv_obj_align(label, LV_ALIGN_TOP_LEFT, 66, 12);
    label = lv_label_create(button); lv_label_set_text(label, description); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(MUTED), 0); lv_obj_set_width(label, width - 78); lv_obj_set_height(label, height - 48); lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP); lv_obj_align(label, LV_ALIGN_TOP_LEFT, 66, 42);
    create_arcade_marker(button, 66, height - 9, width - 84, icon_color);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(button, demo_menu_event, LV_EVENT_ALL, reinterpret_cast<void *>(static_cast<uintptr_t>(target)));
}

void create_student_menu_button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, uint32_t color, uint32_t icon_color,
                                const char *title, const char *description, Pantalla target)
{
    create_student_menu_button_sized(parent, x, y, 350, 104, color, icon_color, title, description, target);
}

void create_arcade_student_home(lv_obj_t *screen)
{
    create_student_topbar(screen, "Inicio", true, true);
    lv_obj_t *balance = make_content_panel(screen, 18, 112, 764, 92, 0xE8F0FF);
    lv_obj_t *avatar = make_shape(balance, 58, 58, SKY); lv_obj_align(avatar, LV_ALIGN_LEFT_MID, 18, 0);
    lv_obj_t *face = make_shape(avatar, 40, 40, YELLOW); lv_obj_align(face, LV_ALIGN_CENTER, 0, 5);
    lv_obj_t *hair = make_shape(face, 38, 15, ORANGE, 8); lv_obj_align(hair, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *eye = make_shape(face, 5, 5, INK); lv_obj_align(eye, LV_ALIGN_TOP_LEFT, 11, 18);
    eye = make_shape(face, 5, 5, INK); lv_obj_align(eye, LV_ALIGN_TOP_RIGHT, -11, 18);
    lv_obj_t *name = lv_label_create(balance); lv_label_set_text(name, selected_student->preferred_name ? selected_student->preferred_name : selected_student->name); lv_obj_set_style_text_font(name, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(name, lv_color_hex(INK), 0); lv_obj_align(name, LV_ALIGN_TOP_LEFT, 94, 16);
    char grade_group[24]; snprintf(grade_group, sizeof(grade_group), "%u.\xC2\xBA %s", selected_student->grade, selected_student->group ? selected_student->group : "");
    lv_obj_t *grade = lv_label_create(balance); lv_label_set_text(grade, grade_group); lv_obj_set_style_text_font(grade, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(grade, lv_color_hex(PURPLE), 0); lv_obj_align(grade, LV_ALIGN_TOP_LEFT, 94, 48);
    lv_obj_t *level = lv_label_create(balance); lv_label_set_text(level, selected_student->level ? selected_student->level : "Nivel pendiente"); lv_obj_set_style_text_font(level, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(level, lv_color_hex(MUTED), 0); lv_obj_align(level, LV_ALIGN_TOP_LEFT, 220, 48);
    lv_obj_t *currency_badge = lv_obj_create(balance); lv_obj_remove_style_all(currency_badge); lv_obj_set_size(currency_badge, 224, 70); lv_obj_align(currency_badge, LV_ALIGN_RIGHT_MID, -16, 0); apply_arcade_badge_style(currency_badge, 0xFFF4D8, ORANGE);
    lv_obj_t *coin = make_shape(currency_badge, 38, 38, YELLOW); lv_obj_align(coin, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *coin_label = lv_label_create(coin); lv_label_set_text(coin_label, "A"); lv_obj_set_style_text_font(coin_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(coin_label, lv_color_hex(INK), 0); lv_obj_center(coin_label);
    lv_obj_t *caption = lv_label_create(currency_badge); lv_label_set_text(caption, "SALDO"); lv_obj_set_style_text_font(caption, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(caption, lv_color_hex(ORANGE), 0); lv_obj_align(caption, LV_ALIGN_TOP_LEFT, 62, 8);
    balance_amount_label = lv_label_create(currency_badge); lv_obj_set_style_text_font(balance_amount_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(balance_amount_label, lv_color_hex(INK), 0); lv_obj_align(balance_amount_label, LV_ALIGN_BOTTOM_LEFT, 62, -8);
    balance_unit_label = lv_label_create(currency_badge); lv_label_set_text(balance_unit_label, CURRENCY_NAME); lv_obj_set_style_text_font(balance_unit_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(balance_unit_label, lv_color_hex(MUTED), 0); lv_obj_align(balance_unit_label, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
    updateBalanceUI();
    animate_balance_card(balance);
    create_student_menu_button_sized(screen, 18, 226, 230, 96, 0xDDF3FF, BLUE, "Mi cuenta", "Consulta tus movimientos", PANTALLA_CUENTA);
    create_student_menu_button_sized(screen, 285, 226, 230, 96, 0xFFF4D8, ORANGE, "Mi progreso", "Mira tu avance escolar", PANTALLA_PROGRESO);
    create_student_menu_button_sized(screen, 552, 226, 230, 96, 0xE8F0FF, BLUE, "Transferir", "Env\xC3\xAD" "a saldo", PANTALLA_TRANSFERIR);
    create_student_menu_button_sized(screen, 148, 338, 230, 96, 0xFBEAF2, PURPLE, "Mis metas", "Mira cu\xC3\xA1nto has avanzado", PANTALLA_METAS);
    create_student_menu_button_sized(screen, 408, 338, 230, 96, 0xE4F7EC, GREEN, "Logros", "Descubre lo que has conseguido", PANTALLA_LOGROS);
}

void create_student_home(lv_obj_t *screen)
{
    create_arcade_student_home(screen);
    return;
    char greeting[96];
    snprintf(greeting, sizeof(greeting), "\xC2\xA1Hola, %s!", selected_student->preferred_name ? selected_student->preferred_name : selected_student->name);
    create_student_topbar(screen, greeting, true, true);
    lv_obj_t *balance = lv_obj_create(screen); lv_obj_remove_style_all(balance); lv_obj_set_size(balance, 180, 108); set_panel_style(balance, lv_color_hex(0xFFF4D8), 24); lv_obj_align(balance, LV_ALIGN_TOP_LEFT, 300, 24);
    char balance_caption[48];
    snprintf(balance_caption, sizeof(balance_caption), "Mis %s", CURRENCY_NAME);
    lv_obj_t *caption = lv_label_create(balance); lv_label_set_text(caption, balance_caption); lv_obj_set_style_text_color(caption, lv_color_hex(ORANGE), 0); lv_obj_align(caption, LV_ALIGN_TOP_LEFT, 22, 14);
    balance_amount_label = lv_label_create(balance); lv_obj_set_style_text_font(balance_amount_label, &lv_font_montserrat_30, 0); lv_obj_set_style_text_color(balance_amount_label, lv_color_hex(INK), 0); lv_obj_align(balance_amount_label, LV_ALIGN_BOTTOM_LEFT, 20, -10);
    balance_unit_label = lv_label_create(balance); lv_label_set_text(balance_unit_label, CURRENCY_NAME); lv_obj_set_style_text_color(balance_unit_label, lv_color_hex(MUTED), 0); lv_obj_align(balance_unit_label, LV_ALIGN_BOTTOM_RIGHT, -16, -20);
    updateBalanceUI();
    animate_balance_card(balance);
    create_student_menu_button_sized(screen, 32, 208, 230, 96, 0xDDF3FF, BLUE, "Mi cuenta", "Consulta tus movimientos", PANTALLA_CUENTA);
    create_student_menu_button_sized(screen, 285, 208, 230, 96, 0xFFF4D8, ORANGE, "Mi progreso", "Mira tu avance escolar", PANTALLA_PROGRESO);
    create_student_menu_button_sized(screen, 538, 208, 230, 96, 0xE8F0FF, BLUE, "Transferir", "Envía saldo", PANTALLA_TRANSFERIR);
    create_student_menu_button_sized(screen, 155, 328, 230, 96, 0xFBEAF2, PURPLE, "Mis metas", "Mira cuánto has avanzado", PANTALLA_METAS);
    create_student_menu_button_sized(screen, 415, 328, 230, 96, 0xE4F7EC, GREEN, "Logros", "Descubre lo que has conseguido", PANTALLA_LOGROS);
}

void demo_back_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        register_student_activity();
        Pantalla destination = PANTALLA_ALUMNO;
        if (pantalla_actual == PANTALLA_CONFIGURACION) destination = PANTALLA_ESPERA;
        else if (pantalla_actual == PANTALLA_FLUIDEZ || pantalla_actual == PANTALLA_DICTADO) destination = PANTALLA_PROGRESO;
        mostrar_pantalla(destination);
    }
}

void demo_exit_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        register_student_activity();
        mostrar_pantalla(PANTALLA_ESPERA);
    }
}

void config_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (!visual_effects_enabled || config_gear_canvas == nullptr) {
        mostrar_pantalla(PANTALLA_CONFIGURACION);
        return;
    }
    if (config_gear_animating) return;

    config_gear_animating = true;
    lv_anim_t gear_anim;
    lv_anim_init(&gear_anim);
    lv_anim_set_var(&gear_anim, config_gear_canvas);
    lv_anim_set_values(&gear_anim, config_gear_angle,
                       config_gear_angle + CONFIG_GEAR_ROTATION_DEG10);
    lv_anim_set_time(&gear_anim, CONFIG_GEAR_ROTATION_MS);
    lv_anim_set_path_cb(&gear_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&gear_anim, config_gear_rotation_exec);
    lv_anim_set_ready_cb(&gear_anim, [](lv_anim_t *) {
        config_gear_animating = false;
        if (pantalla_actual != PANTALLA_ESPERA) return;
        if (config_gear_canvas) lv_anim_del(config_gear_canvas, config_gear_rotation_exec);
        config_gear_canvas = nullptr;
        mostrar_pantalla(PANTALLA_CONFIGURACION);
    });
    lv_anim_start(&gear_anim);
}

void config_theme_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    current_theme = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)) == 1 ? AppTheme::DARK : AppTheme::LIGHT;
    register_student_activity();
    mostrar_pantalla(PANTALLA_CONFIGURACION);
}

void config_performance_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    performance_monitor_enabled = !performance_monitor_enabled;
    register_student_activity();
    mostrar_pantalla(PANTALLA_CONFIGURACION);
}

void config_effects_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    visual_effects_enabled = !visual_effects_enabled;
    register_student_activity();
    mostrar_pantalla(PANTALLA_CONFIGURACION);
}

void config_wifi_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    wifi_manager.cancelPendingConnection();
    wifi_selected_ssid = "";
    wifi_manual_network = false;
    mostrar_pantalla(PANTALLA_WIFI_REDES);
}

void wifi_scan_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (wifi_scan_list) lv_obj_clean(wifi_scan_list);
    wifi_rendered_scan_revision = UINT32_MAX;
    wifi_manager.startScan();
    if (wifi_scan_status_label) {
        lv_label_set_text(wifi_scan_status_label, wifi_manager.isScanInProgress() ?
                          "Escaneando redes..." : "No se pudo iniciar el escaneo.");
    }
}

void wifi_other_network_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    wifi_selected_ssid = "";
    wifi_selected_secured = true;
    wifi_manual_network = true;
    mostrar_pantalla(PANTALLA_WIFI_PASSWORD);
}

void wifi_forget_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED && wifi_manager.hasSavedCredentials()) {
        mostrar_pantalla(PANTALLA_WIFI_OLVIDAR);
    }
}

void wifi_forget_confirm_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    wifi_manager.clearCredentials();
    mostrar_pantalla(PANTALLA_CONFIGURACION);
}

void wifi_network_select_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const size_t index = static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    const WifiNetworkInfo network = wifi_manager.getScanResult(index);
    if (network.ssid.length() == 0) return;
    wifi_selected_ssid = network.ssid;
    wifi_selected_secured = network.secured;
    wifi_manual_network = false;
    if (network.secured) {
        mostrar_pantalla(PANTALLA_WIFI_PASSWORD);
    } else if (wifi_manager.connectToNetwork(network.ssid, "")) {
        mostrar_pantalla(PANTALLA_WIFI_PROGRESS);
    }
}

void wifi_password_cancel_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (pantalla_actual == PANTALLA_WIFI_PASSWORD) {
        wifi_selected_ssid = "";
        wifi_manual_network = false;
        mostrar_pantalla(PANTALLA_WIFI_REDES);
    } else if (pantalla_actual == PANTALLA_WIFI_OLVIDAR) {
        mostrar_pantalla(PANTALLA_WIFI_REDES);
    } else if (pantalla_actual == PANTALLA_WIFI_REDES) {
        mostrar_pantalla(PANTALLA_CONFIGURACION);
    }
}

void wifi_connect_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const String ssid = wifi_manual_network && wifi_manual_ssid_textarea ?
        String(lv_textarea_get_text(wifi_manual_ssid_textarea)) : wifi_selected_ssid;
    const String password = wifi_password_textarea ?
        String(lv_textarea_get_text(wifi_password_textarea)) : String();
    if (ssid.length() == 0) {
        if (wifi_connect_status_label) lv_label_set_text(wifi_connect_status_label, "Escribe el nombre de la red.");
        return;
    }
    if (!wifi_manager.connectToNetwork(ssid, password)) {
        if (wifi_connect_status_label) lv_label_set_text(wifi_connect_status_label, "Revisa el nombre o la contraseña.");
        return;
    }
    if (wifi_password_textarea) lv_textarea_set_text(wifi_password_textarea, "");
    if (wifi_manual_ssid_textarea) lv_textarea_set_text(wifi_manual_ssid_textarea, "");
    wifi_selected_ssid = ssid;
    wifi_manual_network = false;
    mostrar_pantalla(PANTALLA_WIFI_PROGRESS);
}

void wifi_textarea_focus_event(lv_event_t *event);

void wifi_progress_cancel_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    wifi_manager.cancelPendingConnection();
    wifi_selected_ssid = "";
    mostrar_pantalla(PANTALLA_WIFI_REDES);
}

void wifi_retry_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (wifi_manager.retryPendingConnection()) mostrar_pantalla(PANTALLA_WIFI_PROGRESS);
}

void wifi_change_network_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    wifi_manager.cancelPendingConnection();
    wifi_selected_ssid = "";
    mostrar_pantalla(PANTALLA_WIFI_REDES);
}

void wifi_result_cancel_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    wifi_manager.cancelPendingConnection();
    mostrar_pantalla(PANTALLA_CONFIGURACION);
}

void wifi_result_accept_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) mostrar_pantalla(PANTALLA_CONFIGURACION);
}

void create_back_button(lv_obj_t *screen)
{
    lv_obj_t *back = lv_obj_create(screen); lv_obj_remove_style_all(back); lv_obj_set_size(back, 132, 50); apply_arcade_button_style(back, 0xE8F7FF, BLUE); lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 28, -16);
    lv_obj_t *label = lv_label_create(back); lv_label_set_text(label, "Volver"); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0); lv_obj_center(label);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(back, arcade_button_feedback_event, LV_EVENT_ALL, nullptr); lv_obj_add_event_cb(back, demo_back_event, LV_EVENT_CLICKED, nullptr);
}

void create_theme_option(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const char *title, AppTheme option)
{
    const bool selected = current_theme == option;
    lv_obj_t *button = lv_obj_create(parent); lv_obj_remove_style_all(button); lv_obj_set_size(button, 150, 40);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, x, y);
    set_panel_style(button, selected ? lv_color_hex(0xDDF3FF) : lv_color_hex(0xFFFFFF), 18);
    lv_obj_set_style_border_width(button, selected ? 3 : 1, 0); lv_obj_set_style_border_color(button, selected ? lv_color_hex(BLUE) : lv_color_hex(0xD8EEF7), 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(button, config_theme_event, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(option == AppTheme::DARK ? 1 : 0)));
    lv_obj_t *label = lv_label_create(button); lv_label_set_text(label, title); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0); lv_obj_center(label);
}

void create_config_toggle(lv_obj_t *parent, lv_coord_t y, const char *title, const char *description,
                          bool enabled, lv_event_cb_t callback)
{
    lv_obj_t *row = lv_obj_create(parent); lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 336, 68); lv_obj_align(row, LV_ALIGN_TOP_LEFT, 12, y);
    set_panel_style(row, enabled ? lv_color_hex(0xE4F7EC) : lv_color_hex(0xF2F8FF), 14);
    lv_obj_set_style_border_width(row, 1, 0); lv_obj_set_style_border_color(row, lv_color_hex(0xD8EEF7), 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *label = lv_label_create(row); lv_label_set_text(label, title);
    lv_obj_set_width(label, 310); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 12, 4);
    label = lv_label_create(row); lv_label_set_text(label, description);
    lv_obj_set_width(label, 310); lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0); lv_obj_set_style_text_color(label, lv_color_hex(MUTED), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 12, 25);

    label = lv_label_create(row); lv_label_set_text(label, enabled ? "ACTIVADO" : "DESACTIVADO");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0); lv_obj_set_style_text_color(label, enabled ? lv_color_hex(GREEN) : lv_color_hex(MUTED), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 12, -5);
}

lv_obj_t *create_wifi_action_button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                    lv_coord_t width, lv_coord_t height,
                                    const char *text, lv_event_cb_t callback,
                                    uint32_t background = 0xDDF3FF,
                                    uint32_t accent = BLUE)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    apply_arcade_button_style(button, background, accent, true);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, arcade_button_feedback_event, LV_EVENT_ALL, nullptr);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(INK), 0);
    lv_obj_center(label);
    return button;
}

void create_config_screen(lv_obj_t *screen)
{
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *header = lv_obj_create(screen); lv_obj_remove_style_all(header); lv_obj_set_size(header, SCREEN_WIDTH, 102); apply_arcade_header_style(header);
    lv_obj_t *accent = make_shape(header, 12, 62, ORANGE, 6); lv_obj_align(accent, LV_ALIGN_TOP_LEFT, 24, 18);
    lv_obj_t *title = lv_label_create(header); lv_label_set_text(title, "Configuración"); lv_obj_set_style_text_font(title, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(title, lv_color_hex(INK), 0); lv_obj_align(title, LV_ALIGN_TOP_LEFT, 48, 22);
    lv_obj_t *subtitle = lv_label_create(header); lv_label_set_text(subtitle, "Ajustes de Banco Escolar"); lv_obj_set_style_text_font(subtitle, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(subtitle, lv_color_hex(MUTED), 0); lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 48, 58);

    lv_obj_t *panel = make_content_panel(screen, 18, 108, 764, 300, 0xFBEAF2);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left = lv_obj_create(panel); lv_obj_remove_style_all(left); lv_obj_set_size(left, 360, 280); lv_obj_set_pos(left, 12, 10);
    set_panel_style(left, lv_color_hex(theme_palette().surface), 14); lv_obj_set_style_border_width(left, 1, 0); lv_obj_set_style_border_color(left, lv_color_hex(theme_palette().border), 0); lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *section = lv_label_create(left); lv_label_set_text(section, "APARIENCIA"); lv_obj_set_style_text_font(section, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(section, lv_color_hex(PURPLE), 0); lv_obj_align(section, LV_ALIGN_TOP_LEFT, 14, 8);
    lv_obj_t *hint = lv_label_create(left); lv_label_set_text(hint, "Tema de pantalla"); lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0); lv_obj_set_style_text_color(hint, lv_color_hex(INK), 0); lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 14, 30);
    create_theme_option(left, 14, 52, "CLARO", AppTheme::LIGHT);
    create_theme_option(left, 176, 52, "OSCURO", AppTheme::DARK);
    lv_obj_t *theme_state = lv_label_create(left);
    lv_label_set_text(theme_state, current_theme == AppTheme::LIGHT ? "Seleccionado: Claro" : "Seleccionado: Oscuro");
    lv_obj_set_style_text_font(theme_state, &lv_font_montserrat_14, 0); lv_obj_set_style_text_color(theme_state, lv_color_hex(BLUE), 0); lv_obj_align(theme_state, LV_ALIGN_TOP_LEFT, 14, 96);

    create_config_toggle(left, 120, "MONITOR DE RENDIMIENTO", "FPS, RAM y PSRAM", performance_monitor_enabled, config_performance_event);
    create_config_toggle(left, 202, "EFECTOS VISUALES", "Partículas y animaciones suaves", visual_effects_enabled, config_effects_event);

    lv_obj_t *network = lv_obj_create(panel); lv_obj_remove_style_all(network); lv_obj_set_size(network, 350, 280); lv_obj_set_pos(network, 390, 10); set_panel_style(network, lv_color_hex(theme_palette().surface), 14); lv_obj_clear_flag(network, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(network, 1, 0); lv_obj_set_style_border_color(network, lv_color_hex(theme_palette().border), 0);
    lv_obj_t *network_title = lv_label_create(network); lv_label_set_text(network_title, "RED"); lv_obj_set_style_text_font(network_title, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(network_title, lv_color_hex(BLUE), 0); lv_obj_set_style_text_opa(network_title, LV_OPA_COVER, 0); lv_obj_align(network_title, LV_ALIGN_TOP_LEFT, 16, 10);
    const char *network_keys[] = {"Estado", "Red", "IP", "Se\xC3\xB1" "al"};
    lv_obj_t **network_values[] = {&wifi_config_state_label, &wifi_config_ssid_label, &wifi_config_ip_label, &wifi_config_rssi_label};
    for (int i = 0; i < 4; ++i) {
        const lv_coord_t y = 42 + i * 32;
        lv_obj_t *key = lv_label_create(network); lv_label_set_text(key, network_keys[i]); lv_obj_set_style_text_font(key, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(key, lv_color_hex(theme_palette().secondary_text), 0); lv_obj_set_style_text_opa(key, LV_OPA_COVER, 0); lv_obj_set_size(key, 76, 28); lv_obj_set_pos(key, 16, y);
        *network_values[i] = lv_label_create(network); lv_label_set_text(*network_values[i], "--"); lv_obj_set_style_text_font(*network_values[i], &banco_escolar_font_16, 0); lv_obj_set_style_text_color(*network_values[i], lv_color_hex(theme_palette().primary_text), 0); lv_obj_set_style_text_opa(*network_values[i], LV_OPA_COVER, 0); lv_obj_set_size(*network_values[i], 238, 28); lv_label_set_long_mode(*network_values[i], LV_LABEL_LONG_DOT); lv_obj_set_pos(*network_values[i], 100, y);
    }
    create_wifi_action_button(network, 14, 172, 320, 44, "CONFIGURAR WI-FI", config_wifi_event);
    create_wifi_action_button(network, 14, 222, 320, 44, "ALMACENAMIENTO LOCAL", config_storage_event);

    create_back_button(screen);
    update_wifi_ui();
}

void format_storage_size(size_t bytes, char *buffer, size_t buffer_size)
{
    if (bytes < 1024) {
        snprintf(buffer, buffer_size, "%u bytes", static_cast<unsigned>(bytes));
        return;
    }

    const uint64_t scaled_bytes = static_cast<uint64_t>(bytes);
    if (bytes < 1024 * 1024) {
        const uint64_t tenths_kb = (scaled_bytes * 10 + 512) / 1024;
        snprintf(buffer, buffer_size, "%llu.%llu KB",
                 static_cast<unsigned long long>(tenths_kb / 10),
                 static_cast<unsigned long long>(tenths_kb % 10));
        return;
    }

    const uint64_t tenths_mb = (scaled_bytes * 10 + (1024 * 1024) / 2) / (1024 * 1024);
    snprintf(buffer, buffer_size, "%llu.%llu MB",
             static_cast<unsigned long long>(tenths_mb / 10),
             static_cast<unsigned long long>(tenths_mb % 10));
}

uint8_t storage_usage_percent(size_t total, size_t used)
{
    if (total == 0) return 0;
    const uint64_t safe_used = used > total ? total : used;
    return static_cast<uint8_t>((safe_used * 100) / total);
}

void storage_back_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    register_student_activity();
    storage_status_message = nullptr;
    mostrar_pantalla(PANTALLA_CONFIGURACION);
}

void config_storage_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    register_student_activity();
    storage_status_message = nullptr;
    mostrar_pantalla(PANTALLA_STORAGE_LOCAL);
}

void storage_delete_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !storage_manager.isReady()) return;
    register_student_activity();
    mostrar_pantalla(PANTALLA_STORAGE_CONFIRMAR);
}

void storage_confirm_continue_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    register_student_activity();
    mostrar_pantalla(PANTALLA_STORAGE_CONFIRMAR_FINAL);
}

void storage_confirm_cancel_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    register_student_activity();
    mostrar_pantalla(PANTALLA_STORAGE_LOCAL);
}

void storage_delete_final_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    register_student_activity();
    const StorageResult result = storage_manager.clearLocalData();
    storage_status_success = result == StorageResult::STORAGE_OK;
    storage_status_message = storage_status_success ? "Datos locales eliminados" :
                                                      "No se pudieron eliminar los datos";
    if (!storage_status_success) {
        Serial.printf("[Storage] Error en borrado confirmado: resultado %u\n",
                      static_cast<unsigned>(result));
    }
    mostrar_pantalla(PANTALLA_STORAGE_LOCAL);
}

lv_obj_t *create_storage_action_button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                       lv_coord_t width, lv_coord_t height,
                                       const char *text, lv_event_cb_t callback,
                                       uint32_t background, uint32_t accent,
                                       bool enabled = true)
{
    lv_obj_t *button = create_wifi_action_button(parent, x, y, width, height, text,
                                                  callback, background, accent);
    if (!enabled) {
        lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_state(button, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(button, lv_color_hex(0xD4DDE5), 0);
        lv_obj_set_style_border_color(button, lv_color_hex(0xAAB8C5), 0);
    }
    return button;
}

void create_storage_metric_block(lv_obj_t *screen, lv_coord_t y, const char *title,
                                 const char *state_text, uint32_t state_color,
                                 bool ready, size_t total, size_t used, size_t free,
                                 uint32_t accent)
{
    lv_obj_t *card = make_content_panel(screen, 24, y, 752, 124, 0xE8F7FF);
    lv_obj_t *heading = lv_label_create(card);
    lv_label_set_text(heading, title);
    lv_obj_set_style_text_font(heading, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(accent), 0);
    lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 14, 7);

    lv_obj_t *state = lv_label_create(card);
    lv_label_set_text(state, state_text);
    lv_obj_set_style_text_font(state, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(state, lv_color_hex(state_color), 0);
    lv_obj_set_width(state, 330);
    lv_label_set_long_mode(state, LV_LABEL_LONG_DOT);
    lv_obj_align(state, LV_ALIGN_TOP_RIGHT, -14, 8);

    const lv_coord_t columns[] = {16, 260, 504};
    const char *captions[] = {"TOTAL", "USADO", "LIBRE"};
    const size_t values[] = {total, used, free};
    for (size_t i = 0; i < 3; ++i) {
        lv_obj_t *caption = lv_label_create(card);
        lv_label_set_text(caption, captions[i]);
        lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(caption, lv_color_hex(MUTED), 0);
        lv_obj_align(caption, LV_ALIGN_TOP_LEFT, columns[i], 34);

        char size_text[24];
        if (ready) format_storage_size(values[i], size_text, sizeof(size_text));
        else snprintf(size_text, sizeof(size_text), "--");
        lv_obj_t *value = lv_label_create(card);
        lv_label_set_text(value, size_text);
        lv_obj_set_style_text_font(value, &banco_escolar_font_16, 0);
        lv_obj_set_style_text_color(value, lv_color_hex(theme_palette().primary_text), 0);
        lv_obj_set_width(value, 220);
        lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
        lv_obj_align(value, LV_ALIGN_TOP_LEFT, columns[i], 53);
    }

    const uint8_t percent = ready ? storage_usage_percent(total, used) : 0;
    char percent_text[16];
    if (ready) snprintf(percent_text, sizeof(percent_text), "%u%%", static_cast<unsigned>(percent));
    else snprintf(percent_text, sizeof(percent_text), "--");
    lv_obj_t *percent_label = lv_label_create(card);
    lv_label_set_text(percent_label, percent_text);
    lv_obj_set_style_text_font(percent_label, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(percent_label, lv_color_hex(accent), 0);
    lv_obj_set_width(percent_label, 112);
    lv_obj_align(percent_label, LV_ALIGN_TOP_RIGHT, -14, 88);

    lv_obj_t *bar = lv_bar_create(card);
    lv_obj_set_size(bar, 580, 18);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xC5DCEB), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 9, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 9, LV_PART_INDICATOR);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 16, 91);
    if (!ready || total == 0) lv_obj_add_state(bar, LV_STATE_DISABLED);
}

const char *sd_state_label(SdState state)
{
    switch (state) {
        case SdState::SD_NOT_PRESENT: return "Estado: No instalada";
        case SdState::SD_MOUNTING: return "Estado: Preparando";
        case SdState::SD_READY: return "Estado: Disponible";
        case SdState::SD_ERROR: return "Estado: No disponible";
    }
    return "Estado: No disponible";
}

uint32_t sd_state_color(SdState state)
{
    if (state == SdState::SD_READY) return GREEN;
    if (state == SdState::SD_ERROR) return ORANGE;
    return MUTED;
}

void create_storage_screen(lv_obj_t *screen)
{
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    create_wifi_page_header(screen, "ALMACENAMIENTO LOCAL");

    const bool internal_ready = storage_manager.isReady();
    create_storage_metric_block(screen, 94, "MEMORIA INTERNA",
        internal_ready ? "Estado: Disponible" : "Estado: No disponible",
        internal_ready ? GREEN : ORANGE,
        internal_ready,
        internal_ready ? storage_manager.getTotalBytes() : 0,
        internal_ready ? storage_manager.getUsedBytes() : 0,
        internal_ready ? storage_manager.getFreeBytes() : 0,
        BLUE);

    const SdState sd_state = sd_manager.state();
    const bool sd_ready = sd_manager.isReady();
    create_storage_metric_block(screen, 226, "TARJETA MICROSD",
        sd_state_label(sd_state), sd_state_color(sd_state), sd_ready,
        sd_ready ? sd_manager.totalBytes() : 0,
        sd_ready ? sd_manager.usedBytes() : 0,
        sd_ready ? sd_manager.freeBytes() : 0,
        PURPLE);

    lv_obj_t *description = lv_label_create(screen);
    lv_label_set_text(description, storage_status_message != nullptr ? storage_status_message :
        (internal_ready ? "El almacenamiento local conserva datos aunque el equipo se reinicie." :
                          "Almacenamiento interno no disponible"));
    lv_obj_set_style_text_font(description, &lv_font_montserrat_14, 0);
    const uint32_t description_color = storage_status_message != nullptr ?
        (storage_status_success ? GREEN : ORANGE) : (internal_ready ? MUTED : ORANGE);
    lv_obj_set_style_text_color(description, lv_color_hex(description_color), 0);
    lv_obj_set_width(description, 752);
    lv_label_set_long_mode(description, LV_LABEL_LONG_DOT);
    lv_obj_align(description, LV_ALIGN_TOP_LEFT, 24, 357);

    create_storage_action_button(screen, 268, 380, 264, 38, "ELIMINAR DATOS",
                                 storage_delete_event, 0xFBEAF2, 0xD64D59, internal_ready);
    create_storage_action_button(screen, 28, 420, 132, 44, "VOLVER",
                                 storage_back_event, 0xE8F7FF, BLUE);
}

void create_storage_confirmation_screen(lv_obj_t *screen, bool final_step)
{
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    create_wifi_page_header(screen, final_step ? "CONFIRMACIÓN FINAL" : "ELIMINAR DATOS");

    lv_obj_t *card = make_content_panel(screen, 74, 112, 652, 226, 0xFBEAF2);
    lv_obj_t *message = lv_label_create(card);
    lv_label_set_text(message, final_step ?
        "Esta acción no se puede deshacer.\n¿Seguro que deseas eliminar los datos locales?" :
        "¿Eliminar los datos locales?\nSe borrarán los registros guardados en el almacenamiento interno.");
    lv_obj_set_style_text_font(message, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(message, lv_color_hex(theme_palette().primary_text), 0);
    lv_obj_set_width(message, 580);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_CENTER, 0, 0);

    if (final_step) {
        create_storage_action_button(screen, 204, 370, 180, 50, "CANCELAR",
                                     storage_confirm_cancel_event, 0xE8F7FF, BLUE);
        create_storage_action_button(screen, 416, 370, 180, 50, "ELIMINAR",
                                     storage_delete_final_event, 0xFBEAF2, 0xC62828);
    } else {
        create_storage_action_button(screen, 204, 370, 180, 50, "CANCELAR",
                                     storage_confirm_cancel_event, 0xE8F7FF, BLUE);
        create_storage_action_button(screen, 416, 370, 180, 50, "CONTINUAR",
                                     storage_confirm_continue_event, 0xFFF4D8, ORANGE);
    }
}

void create_wifi_page_header(lv_obj_t *screen, const char *title)
{
    lv_obj_t *header = lv_obj_create(screen);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, SCREEN_WIDTH, 58);
    apply_arcade_header_style(header);
    lv_obj_t *label = lv_label_create(header);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(theme_palette().primary_text), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 24, 0);
}

void create_wifi_network_row(lv_obj_t *parent, size_t index)
{
    const WifiNetworkInfo network = wifi_manager.getScanResult(index);
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 728, 56);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 8 + static_cast<lv_coord_t>(index * 64));
    apply_arcade_button_style(row, 0xE8F0FF, BLUE, true);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, arcade_button_feedback_event, LV_EVENT_ALL, nullptr);
    lv_obj_add_event_cb(row, wifi_network_select_event, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(index)));

    lv_obj_t *ssid = lv_label_create(row);
    lv_label_set_text(ssid, network.ssid.c_str());
    lv_obj_set_style_text_font(ssid, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(ssid, lv_color_hex(theme_palette().primary_text), 0);
    lv_obj_set_width(ssid, 690);
    lv_label_set_long_mode(ssid, LV_LABEL_LONG_DOT);
    lv_obj_align(ssid, LV_ALIGN_TOP_LEFT, 16, 5);

    char detail[48];
    snprintf(detail, sizeof(detail), "%ld dBm | %s", static_cast<long>(network.rssi),
             network.secured ? "PROTEGIDA" : "ABIERTA");
    lv_obj_t *info = lv_label_create(row);
    lv_label_set_text(info, detail);
    lv_obj_set_style_text_font(info, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(theme_palette().secondary_text), 0);
    lv_obj_align(info, LV_ALIGN_BOTTOM_LEFT, 16, -4);
}

void refresh_wifi_network_list()
{
    if (wifi_scan_list == nullptr) return;
    lv_obj_clean(wifi_scan_list);
    const size_t count = wifi_manager.getScanResultCount();
    for (size_t i = 0; i < count; ++i) create_wifi_network_row(wifi_scan_list, i);
}

void create_wifi_network_screen(lv_obj_t *screen)
{
    create_wifi_page_header(screen, "REDES WI-FI");
    create_wifi_action_button(screen, 24, 72, 160, 46, "ESCANEAR", wifi_scan_event);
    create_wifi_action_button(screen, 198, 72, 180, 46, "OTRA RED", wifi_other_network_event,
                              0xFFF4D8, ORANGE);
    if (wifi_manager.hasSavedCredentials()) {
        create_wifi_action_button(screen, 582, 72, 194, 46, "OLVIDAR RED", wifi_forget_event,
                                  0xFBEAF2, PINK);
    }

    wifi_scan_status_label = lv_label_create(screen);
    lv_label_set_text(wifi_scan_status_label, "Toca ESCANEAR para buscar redes.");
    lv_obj_set_style_text_font(wifi_scan_status_label, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(wifi_scan_status_label, lv_color_hex(theme_palette().secondary_text), 0);
    lv_obj_set_width(wifi_scan_status_label, 752);
    lv_obj_align(wifi_scan_status_label, LV_ALIGN_TOP_LEFT, 24, 126);

    wifi_scan_list = lv_obj_create(screen);
    lv_obj_remove_style_all(wifi_scan_list);
    lv_obj_set_size(wifi_scan_list, 764, 248);
    lv_obj_set_pos(wifi_scan_list, 18, 154);
    set_panel_style(wifi_scan_list, lv_color_hex(theme_palette().surface), 14);
    lv_obj_set_style_border_width(wifi_scan_list, 1, 0);
    lv_obj_set_style_border_color(wifi_scan_list, lv_color_hex(theme_palette().border), 0);
    lv_obj_set_style_pad_all(wifi_scan_list, 8, 0);
    lv_obj_add_flag(wifi_scan_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(wifi_scan_list, LV_SCROLLBAR_MODE_AUTO);

    create_wifi_action_button(screen, 24, 420, 150, 44, "VOLVER", wifi_password_cancel_event,
                              0xE8F7FF, BLUE);
    wifi_rendered_scan_revision = UINT32_MAX;
    if (wifi_manager.hasScanCompleted()) update_wifi_setup_ui();
}

void wifi_textarea_focus_event(lv_event_t *event)
{
    if (lv_keyboard_get_textarea(wifi_keyboard) != lv_event_get_target(event)) {
        lv_keyboard_set_textarea(wifi_keyboard, lv_event_get_target(event));
    }
}

void create_wifi_password_screen(lv_obj_t *screen)
{
    // Fixed 800x480 layout: prevent the parent screen from scrolling on focus.
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    create_wifi_page_header(screen, "CONECTAR A WI-FI");
    lv_obj_t *ssid_label = lv_label_create(screen);
    lv_obj_set_style_text_font(ssid_label, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(theme_palette().secondary_text), 0);
    lv_obj_t *ssid_value = lv_label_create(screen);
    lv_obj_set_style_text_font(ssid_value, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(ssid_value, lv_color_hex(theme_palette().primary_text), 0);

    if (wifi_manual_network) {
        lv_label_set_text(ssid_label, "Red:");
        lv_obj_set_pos(ssid_label, 20, 68);
        lv_obj_set_size(ssid_label, 48, 24);
        wifi_manual_ssid_textarea = lv_textarea_create(screen);
        lv_obj_set_size(wifi_manual_ssid_textarea, 696, 36);
        lv_obj_set_pos(wifi_manual_ssid_textarea, 80, 60);
        lv_textarea_set_one_line(wifi_manual_ssid_textarea, true);
        lv_textarea_set_max_length(wifi_manual_ssid_textarea, 32);
        lv_textarea_set_placeholder_text(wifi_manual_ssid_textarea, "Nombre de red");
        lv_obj_set_style_text_font(wifi_manual_ssid_textarea, &banco_escolar_font_16, 0);
        lv_obj_add_event_cb(wifi_manual_ssid_textarea, wifi_textarea_focus_event, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(wifi_manual_ssid_textarea, wifi_textarea_focus_event, LV_EVENT_CLICKED, nullptr);
    } else {
        lv_label_set_text(ssid_label, "Red:");
        lv_obj_set_pos(ssid_label, 20, 66);
        lv_obj_set_size(ssid_label, 48, 24);
        lv_label_set_text(ssid_value, wifi_selected_ssid.c_str());
        lv_obj_set_size(ssid_value, 696, 28);
        lv_label_set_long_mode(ssid_value, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(ssid_value, 80, 64);
    }

    lv_obj_t *password_label = lv_label_create(screen);
    lv_label_set_text(password_label, "Contraseña:");
    lv_obj_set_style_text_font(password_label, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(password_label, lv_color_hex(theme_palette().secondary_text), 0);
    lv_obj_set_pos(password_label, 20, 106);
    lv_obj_set_size(password_label, 100, 24);
    wifi_password_textarea = lv_textarea_create(screen);
    lv_obj_set_size(wifi_password_textarea, 648, 38);
    lv_obj_set_pos(wifi_password_textarea, 128, 98);
    lv_textarea_set_one_line(wifi_password_textarea, true);
    lv_textarea_set_max_length(wifi_password_textarea, 63);
    lv_textarea_set_password_mode(wifi_password_textarea, true);
    lv_textarea_set_password_bullet(wifi_password_textarea, "*");
    lv_textarea_set_placeholder_text(wifi_password_textarea, "Contraseña de la red");
    lv_obj_set_style_text_font(wifi_password_textarea, &banco_escolar_font_16, 0);
    lv_obj_add_event_cb(wifi_password_textarea, wifi_textarea_focus_event, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(wifi_password_textarea, wifi_textarea_focus_event, LV_EVENT_CLICKED, nullptr);

    create_wifi_action_button(screen, 24, 144, 150, 44, "CANCELAR", wifi_password_cancel_event,
                              0xFBEAF2, PINK);
    create_wifi_action_button(screen, 626, 144, 150, 44, "CONECTAR", wifi_connect_event,
                              0xE4F7EC, GREEN);
    wifi_connect_status_label = lv_label_create(screen);
    lv_label_set_text(wifi_connect_status_label, "");
    lv_obj_set_style_text_font(wifi_connect_status_label, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(wifi_connect_status_label, lv_color_hex(theme_palette().secondary_text), 0);
    lv_obj_set_size(wifi_connect_status_label, 420, 28);
    lv_obj_set_pos(wifi_connect_status_label, 190, 152);

    wifi_keyboard = lv_keyboard_create(screen);
    lv_obj_set_size(wifi_keyboard, 772, 276);
    // lv_keyboard's constructor applies BOTTOM_MID alignment; reset it so
    // subsequent LVGL layout passes preserve the explicit fixed coordinates.
    lv_obj_set_align(wifi_keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(wifi_keyboard, 14, 194);
    lv_obj_set_style_text_font(wifi_keyboard, &banco_escolar_font_16, 0);
    lv_obj_set_style_bg_color(wifi_keyboard, lv_color_hex(theme_palette().surface), 0);
    // LVGL's stock labels use private-use symbols (PUA), absent from the
    // project's ASCII/Latin font. Use clear ASCII labels with matching actions.
    lv_obj_remove_event_cb(wifi_keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(wifi_keyboard, wifi_keyboard_ascii_event, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_keyboard_set_map(wifi_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER,
                        wifi_kb_map_lower, wifi_kb_ctrl_lower);
    lv_keyboard_set_map(wifi_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER,
                        wifi_kb_map_upper, wifi_kb_ctrl_upper);
    lv_keyboard_set_map(wifi_keyboard, LV_KEYBOARD_MODE_SPECIAL,
                        wifi_kb_map_special, wifi_kb_ctrl_special);
    lv_keyboard_set_mode(wifi_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_popovers(wifi_keyboard, true);
    lv_obj_t *initial_textarea = wifi_manual_network ? wifi_manual_ssid_textarea : wifi_password_textarea;
    lv_keyboard_set_textarea(wifi_keyboard, initial_textarea);
    lv_obj_add_state(initial_textarea, LV_STATE_FOCUSED);
}

void create_wifi_progress_screen(lv_obj_t *screen)
{
    create_wifi_page_header(screen, "CONECTANDO A WI-FI");
    WiFiSnapshot snapshot = wifi_manager.snapshot();
    lv_obj_t *ssid = lv_label_create(screen);
    lv_label_set_text_fmt(ssid, "Red: %s", snapshot.ssid);
    lv_obj_set_style_text_font(ssid, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(ssid, lv_color_hex(theme_palette().primary_text), 0);
    lv_obj_align(ssid, LV_ALIGN_TOP_LEFT, 32, 102);
    wifi_connect_status_label = lv_label_create(screen);
    lv_label_set_text(wifi_connect_status_label, "Conectando...");
    lv_obj_set_style_text_font(wifi_connect_status_label, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(wifi_connect_status_label, lv_color_hex(theme_palette().secondary_text), 0);
    lv_obj_align(wifi_connect_status_label, LV_ALIGN_TOP_LEFT, 32, 156);
    create_wifi_action_button(screen, 24, 420, 170, 44, "CANCELAR", wifi_progress_cancel_event,
                              0xFBEAF2, PINK);
}

void create_wifi_result_screen(lv_obj_t *screen)
{
    const WiFiManualResult result = wifi_manager.manualConnectionResult();
    const WiFiSnapshot snapshot = wifi_manager.snapshot();
    const bool connected = result == WiFiManualResult::SUCCEEDED || result == WiFiManualResult::SAVE_FAILED;
    create_wifi_page_header(screen, connected ? "CONEXION WI-FI" : "NO SE PUDO CONECTAR");

    lv_obj_t *message = lv_label_create(screen);
    lv_label_set_text(message, connected ?
        (result == WiFiManualResult::SUCCEEDED ? "CONEXION EXITOSA" : "Conectada, pero no se pudo guardar la red.") :
        "Revisa la contraseña o la señal de la red.");
    lv_obj_set_style_text_font(message, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(message, lv_color_hex(connected ? GREEN : ORANGE), 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 94);

    if (connected) {
        lv_obj_t *details = lv_label_create(screen);
        lv_label_set_text_fmt(details, "Red: %s\nIP: %s\nSeñal: %ld dBm",
                              snapshot.ssid, snapshot.ip, static_cast<long>(snapshot.rssi));
        lv_obj_set_style_text_font(details, &banco_escolar_font_16, 0);
        lv_obj_set_style_text_color(details, lv_color_hex(theme_palette().primary_text), 0);
        lv_obj_set_style_text_line_space(details, 14, 0);
        lv_obj_align(details, LV_ALIGN_TOP_LEFT, 180, 150);
        create_wifi_action_button(screen, 626, 420, 150, 44, "ACEPTAR", wifi_result_accept_event,
                                  0xE4F7EC, GREEN);
    } else {
        create_wifi_action_button(screen, 24, 420, 220, 44, "REINTENTAR", wifi_retry_event,
                                  0xE4F7EC, GREEN);
        create_wifi_action_button(screen, 290, 420, 220, 44, "CAMBIAR RED", wifi_change_network_event,
                                  0xDDF3FF, BLUE);
        create_wifi_action_button(screen, 556, 420, 220, 44, "CANCELAR", wifi_result_cancel_event,
                                  0xFBEAF2, PINK);
    }
}

void create_wifi_forget_screen(lv_obj_t *screen)
{
    create_wifi_page_header(screen, "OLVIDAR RED");
    lv_obj_t *question = lv_label_create(screen);
    lv_label_set_text(question, "¿OLVIDAR RED GUARDADA?");
    lv_obj_set_style_text_font(question, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(question, lv_color_hex(theme_palette().primary_text), 0);
    lv_obj_align(question, LV_ALIGN_TOP_MID, 0, 160);
    create_wifi_action_button(screen, 210, 260, 170, 48, "CANCELAR", wifi_password_cancel_event,
                              0xE8F0FF, BLUE);
    create_wifi_action_button(screen, 420, 260, 170, 48, "OLVIDAR", wifi_forget_confirm_event,
                              0xFBEAF2, PINK);
}

void update_wifi_setup_ui()
{
    if (pantalla_actual == PANTALLA_WIFI_REDES && wifi_scan_status_label && wifi_scan_list) {
        if (wifi_manager.isScanInProgress()) {
            lv_label_set_text(wifi_scan_status_label, "Escaneando redes...");
        } else if (!wifi_manager.hasScanCompleted()) {
            lv_label_set_text(wifi_scan_status_label, "Toca ESCANEAR para buscar redes.");
        } else if (wifi_rendered_scan_revision != wifi_manager.scanRevision()) {
            wifi_rendered_scan_revision = wifi_manager.scanRevision();
            refresh_wifi_network_list();
            if (wifi_manager.scanFailed()) lv_label_set_text(wifi_scan_status_label, "No se pudo completar el escaneo.");
            else if (wifi_manager.getScanResultCount() == 0) lv_label_set_text(wifi_scan_status_label, "No se encontraron redes. Prueba ESCANEAR otra vez.");
            else lv_label_set_text_fmt(wifi_scan_status_label, "Redes encontradas: %u", static_cast<unsigned>(wifi_manager.getScanResultCount()));
        }
    }

    if (pantalla_actual == PANTALLA_WIFI_PROGRESS && wifi_connect_status_label) {
        const WiFiManualResult result = wifi_manager.manualConnectionResult();
        if (result == WiFiManualResult::SUCCEEDED || result == WiFiManualResult::SAVE_FAILED || result == WiFiManualResult::FAILED) {
            mostrar_pantalla(PANTALLA_WIFI_RESULT);
        }
    }
}

lv_obj_t *make_content_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                             lv_coord_t width, lv_coord_t height, uint32_t color)
{
    lv_obj_t *panel = lv_obj_create(parent); lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, width, height); lv_obj_set_pos(panel, x, y);
    apply_arcade_panel_style(panel, color, 20, BLUE);
    return panel;
}

void set_content_title(lv_obj_t *parent, const char *title)
{
    lv_obj_t *label = lv_label_create(parent); lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(INK), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 24, 18);
}

void create_arcade_account_screen(lv_obj_t *screen)
{
    create_student_topbar(screen, "Mi cuenta", false, false);
    lv_obj_t *balance = make_content_panel(screen, 18, 112, 300, 278, 0xFFF4D8);
    create_arcade_marker(balance, 24, 52, 92, ORANGE);
    lv_obj_t *heading = lv_label_create(balance); lv_label_set_text(heading, "SALDO ACTUAL"); lv_obj_set_style_text_font(heading, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(heading, lv_color_hex(ORANGE), 0); lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 24, 24);
    lv_obj_t *coin = make_shape(balance, 54, 54, YELLOW); lv_obj_align(coin, LV_ALIGN_TOP_LEFT, 24, 74);
    lv_obj_t *coin_label = lv_label_create(coin); lv_label_set_text(coin_label, "A"); lv_obj_set_style_text_font(coin_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(coin_label, lv_color_hex(INK), 0); lv_obj_center(coin_label);
    lv_obj_t *amount = lv_label_create(balance); char amount_text[20]; snprintf(amount_text, sizeof(amount_text), "%ld", static_cast<long>(getStudentBalance()));
    lv_label_set_text(amount, amount_text); lv_obj_set_style_text_font(amount, &lv_font_montserrat_30, 0); lv_obj_set_style_text_color(amount, lv_color_hex(INK), 0); lv_obj_align(amount, LV_ALIGN_TOP_LEFT, 98, 78);
    lv_obj_t *unit = lv_label_create(balance); lv_label_set_text(unit, CURRENCY_NAME); lv_obj_set_style_text_font(unit, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(unit, lv_color_hex(ORANGE), 0); lv_obj_align(unit, LV_ALIGN_TOP_LEFT, 100, 122);
    lv_obj_t *demo = lv_label_create(balance); lv_label_set_text(demo, "Dato DEMO"); lv_obj_set_style_text_font(demo, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(demo, lv_color_hex(MUTED), 0); lv_obj_align(demo, LV_ALIGN_BOTTOM_LEFT, 24, -18);

    lv_obj_t *movements = make_content_panel(screen, 334, 112, 448, 278, 0xE8F7FF);
    lv_obj_t *movement_title = lv_label_create(movements); lv_label_set_text(movement_title, "MOVIMIENTOS RECIENTES"); lv_obj_set_style_text_font(movement_title, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(movement_title, lv_color_hex(BLUE), 0); lv_obj_align(movement_title, LV_ALIGN_TOP_LEFT, 22, 24);
    size_t movement_count = 0; const StudentMovement *student_movements = getStudentMovements(selected_student->student_id, &movement_count);
    for (size_t i = 0; i < movement_count; ++i) {
        lv_obj_t *row = lv_obj_create(movements); lv_obj_remove_style_all(row); lv_obj_set_size(row, 404, 42); lv_obj_align(row, LV_ALIGN_TOP_LEFT, 22, 58 + static_cast<lv_coord_t>(i * 48));
        apply_arcade_badge_style(row, student_movements[i].amount >= 0 ? 0xE4F7EC : 0xFBEAF2, student_movements[i].amount >= 0 ? GREEN : ORANGE);
        char amount_label[20]; snprintf(amount_label, sizeof(amount_label), "%+ld", static_cast<long>(student_movements[i].amount));
        lv_obj_t *sign = lv_label_create(row); lv_label_set_text(sign, amount_label); lv_obj_set_style_text_font(sign, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(sign, lv_color_hex(student_movements[i].amount >= 0 ? GREEN : ORANGE), 0); lv_obj_align(sign, LV_ALIGN_LEFT_MID, 14, 0);
        lv_obj_t *reason = lv_label_create(row); lv_label_set_text(reason, student_movements[i].reason); lv_obj_set_style_text_font(reason, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(reason, lv_color_hex(INK), 0); lv_obj_set_width(reason, 245); lv_label_set_long_mode(reason, LV_LABEL_LONG_DOT); lv_obj_align(reason, LV_ALIGN_LEFT_MID, 70, 0);
        lv_obj_t *date = lv_label_create(row); lv_label_set_text(date, "DEMO"); lv_obj_set_style_text_font(date, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(date, lv_color_hex(MUTED), 0); lv_obj_align(date, LV_ALIGN_RIGHT_MID, -12, 0);
    }
    lv_obj_t *note = lv_label_create(movements); lv_label_set_text(note, "Movimientos temporales de demostraci\xC3\xB3n"); lv_obj_set_style_text_font(note, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(note, lv_color_hex(MUTED), 0); lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 22, -14);
    create_back_button(screen);
}

void create_account_screen(lv_obj_t *screen)
{
    create_arcade_account_screen(screen);
    return;
    create_student_topbar(screen, "Mi cuenta", false, false);
    lv_obj_t *balance = make_content_panel(screen, 40, 120, 300, 228, 0xFFF4D8);
    set_content_title(balance, "Saldo actual");
    lv_obj_t *amount = lv_label_create(balance); char amount_text[20];
    snprintf(amount_text, sizeof(amount_text), "%ld", static_cast<long>(getStudentBalance()));
    lv_label_set_text(amount, amount_text); lv_obj_set_style_text_font(amount, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(amount, lv_color_hex(INK), 0); lv_obj_align(amount, LV_ALIGN_CENTER, 0, 4);
    lv_obj_t *unit = lv_label_create(balance); lv_label_set_text(unit, CURRENCY_NAME);
    lv_obj_set_style_text_font(unit, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(unit, lv_color_hex(ORANGE), 0);
    lv_obj_align(unit, LV_ALIGN_BOTTOM_MID, 0, -54);
    lv_obj_t *demo = lv_label_create(balance); lv_label_set_text(demo, "Dato DEMO");
    lv_obj_set_style_text_color(demo, lv_color_hex(ORANGE), 0); lv_obj_align(demo, LV_ALIGN_BOTTOM_MID, 0, -18);

    lv_obj_t *movements = make_content_panel(screen, 360, 120, 400, 228, 0xE8F7FF);
    set_content_title(movements, "Movimientos de hoy");
    size_t movement_count = 0;
    const StudentMovement *student_movements = getStudentMovements(selected_student->student_id, &movement_count);
    for (size_t i = 0; i < movement_count; ++i) {
        lv_obj_t *row = lv_obj_create(movements); lv_obj_remove_style_all(row); lv_obj_set_size(row, 350, 38);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 24, 58 + static_cast<lv_coord_t>(i * 46));
        const uint32_t row_color = student_movements[i].amount >= 0 ? GREEN : ORANGE;
        lv_obj_t *sign = lv_label_create(row); char amount_label[20];
        snprintf(amount_label, sizeof(amount_label), "%+ld", static_cast<long>(student_movements[i].amount));
        lv_label_set_text(sign, amount_label); lv_obj_set_style_text_font(sign, &banco_escolar_font_16, 0);
        lv_obj_set_style_text_color(sign, lv_color_hex(row_color), 0); lv_obj_align(sign, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t *reason = lv_label_create(row); lv_label_set_text(reason, student_movements[i].reason);
        lv_obj_set_style_text_font(reason, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(reason, lv_color_hex(INK), 0);
        lv_obj_set_width(reason, 260); lv_label_set_long_mode(reason, LV_LABEL_LONG_DOT); lv_obj_align(reason, LV_ALIGN_LEFT_MID, 54, 0);
    }
    lv_obj_t *note = lv_label_create(movements); lv_label_set_text(note, "Movimientos temporales de demostraci\xC3\xB3n");
    lv_obj_set_style_text_color(note, lv_color_hex(MUTED), 0); lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 24, -14);
    create_back_button(screen);
}

void create_progress_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                          uint32_t color, uint32_t icon_color, const char *title,
                          const char *description, Pantalla target)
{
    lv_obj_t *card = make_content_panel(parent, x, y, 340, 132, color);
    lv_obj_t *icon = make_shape(card, 42, 42, icon_color); lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 20, 28);
    lv_obj_t *dot = make_shape(icon, 12, 12, 0xFFFFFF); lv_obj_align(dot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *label = lv_label_create(card); lv_label_set_text(label, title); lv_obj_set_style_text_font(label, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(INK), 0); lv_obj_align(label, LV_ALIGN_TOP_LEFT, 78, 22);
    label = lv_label_create(card); lv_label_set_text(label, description); lv_obj_set_style_text_color(label, lv_color_hex(MUTED), 0); lv_obj_align(label, LV_ALIGN_TOP_LEFT, 78, 68);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(card, demo_menu_event, LV_EVENT_ALL, reinterpret_cast<void *>(static_cast<uintptr_t>(target)));
}

void create_arcade_progress_module(lv_obj_t *parent, lv_coord_t x, uint32_t background,
                                   uint32_t accent, const char *title, const char *description,
                                   const char *value, const char *detail, Pantalla target)
{
    lv_obj_t *card = make_content_panel(parent, x, 122, 370, 254, background);
    create_arcade_marker(card, 24, 58, 86, accent);
    lv_obj_t *icon = make_shape(card, 42, 42, accent); lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 24, 20);
    lv_obj_t *dot = make_shape(icon, 12, 12, 0xFFFFFF); lv_obj_align(dot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *label = lv_label_create(card); lv_label_set_text(label, title); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0); lv_obj_align(label, LV_ALIGN_TOP_LEFT, 82, 24);
    label = lv_label_create(card); lv_label_set_text(label, description); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(MUTED), 0); lv_obj_align(label, LV_ALIGN_TOP_LEFT, 82, 50);
    label = lv_label_create(card); lv_label_set_text(label, value); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(accent), 0); lv_obj_align(label, LV_ALIGN_TOP_LEFT, 24, 94);
    label = lv_label_create(card); lv_label_set_text(label, detail); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0); lv_obj_set_width(label, 320); lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP); lv_obj_align(label, LV_ALIGN_TOP_LEFT, 24, 136);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(card, demo_menu_event, LV_EVENT_ALL, reinterpret_cast<void *>(static_cast<uintptr_t>(target)));
}

void create_arcade_progress_screen(lv_obj_t *screen)
{
    create_student_topbar(screen, "Mi progreso", false, false);
    size_t reading_count = 0; const ReadingRecord *reading = getReadingRecords(selected_student->student_id, &reading_count);
    size_t writing_count = 0; const WritingRecord *writing = getWritingRecords(selected_student->student_id, &writing_count);
    char fluency_value[32] = "Sin registros"; char fluency_detail[64] = "Consulta tus mediciones por fecha";
    if (reading != nullptr && reading_count > 0) { snprintf(fluency_value, sizeof(fluency_value), "%u PPM", reading[reading_count - 1].ppm); snprintf(fluency_detail, sizeof(fluency_detail), "Mejora tu velocidad de lectura"); }
    char dictation_value[32] = "Sin registros"; char dictation_detail[64] = "Consulta tus ejercicios de escritura";
    if (writing != nullptr && writing_count > 0) { const WritingRecord *last = &writing[writing_count - 1]; if (last->applied) snprintf(dictation_value, sizeof(dictation_value), "%d errores", last->errors); else snprintf(dictation_value, sizeof(dictation_value), "N/A"); snprintf(dictation_detail, sizeof(dictation_detail), "Menos errores es mejor"); }
    create_arcade_progress_module(screen, 18, 0xDDF3FF, BLUE, "FLUIDEZ LECTORA", "Palabras por minuto", fluency_value, fluency_detail, PANTALLA_FLUIDEZ);
    create_arcade_progress_module(screen, 412, 0xFFF4D8, ORANGE, "DICTADO", "Seguimiento de errores", dictation_value, dictation_detail, PANTALLA_DICTADO);
    lv_obj_t *hint = lv_label_create(screen); lv_label_set_text(hint, "ELIGE UNA MISIÓN PARA VER MÁS DETALLES"); lv_obj_set_style_text_font(hint, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(hint, lv_color_hex(MUTED), 0); lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 394);
    create_back_button(screen);
}

void create_progress_screen(lv_obj_t *screen)
{
    create_arcade_progress_screen(screen);
    return;
    create_student_topbar(screen, "Mi progreso", false, false);
    create_progress_card(screen, 50, 126, 0xDDF3FF, BLUE, "Fluidez lectora", "Palabras por minuto", PANTALLA_FLUIDEZ);
    create_progress_card(screen, 410, 126, 0xFFF4D8, ORANGE, "Dictado de oraciones", "Seguimiento de errores", PANTALLA_DICTADO);
    lv_obj_t *hint = lv_label_create(screen); lv_label_set_text(hint, "Tus indicadores académicos se mostrarán aquí");
    lv_obj_set_style_text_color(hint, lv_color_hex(MUTED), 0); lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 300);
    create_back_button(screen);
}

void create_month_header(lv_obj_t *screen)
{
    lv_obj_t *month = make_content_panel(screen, 50, 110, 700, 48, 0xE8F0FF);
    lv_obj_t *label = lv_label_create(month); lv_label_set_text(label, "<    Septiembre 2026    >");
    lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0); lv_obj_center(label);
}

lv_color_t fluency_level_color(ReadingLevel level)
{
    switch (level) {
        case ReadingLevel::RequiresSupport: return lv_color_hex(0xD9534F);
        case ReadingLevel::NearStandard: return lv_color_hex(0xD9A620);
        case ReadingLevel::Standard: return lv_color_hex(GREEN);
        case ReadingLevel::Advanced: return lv_color_hex(BLUE);
    }
    return lv_color_hex(MUTED);
}

void create_fluency_legend_item(lv_obj_t *parent, lv_coord_t x, uint32_t color, const char *text)
{
    lv_obj_t *dot = make_shape(parent, 12, 12, color); lv_obj_align(dot, LV_ALIGN_TOP_LEFT, x, 10);
    lv_obj_t *label = lv_label_create(parent); lv_label_set_text(label, text); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(MUTED), 0); lv_obj_align(label, LV_ALIGN_TOP_LEFT, x + 18, 5);
}

void format_record_date_short(const char *date, char *buffer, size_t buffer_size)
{
    static const char *const months[] = {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"};
    int day = 0;
    int month = 0;
    if (date != nullptr && sscanf(date, "%d/%d", &day, &month) == 2 && month >= 1 && month <= 12) {
        snprintf(buffer, buffer_size, "%02d %s", day, months[month - 1]);
        return;
    }
    snprintf(buffer, buffer_size, "%s", date ? date : "N/A");
}

void create_fluency_screen(lv_obj_t *screen)
{
    create_student_topbar(screen, "Fluidez lectora", false, false); create_month_header(screen);
    size_t record_count = 0; const ReadingRecord *records = getReadingRecords(selected_student->student_id, &record_count);
    const uint16_t latest_ppm = records[record_count - 1].ppm;
    const ReadingFluencyEvaluation evaluation = evaluateReadingFluency(selected_student->grade, latest_ppm);
    lv_obj_t *summary = make_content_panel(screen, 50, 168, 700, 62, 0xF7FBFF);
    const char *summary_titles[] = {"ÚLTIMA", "MEJOR", "NIVEL"};
    char summary_values[3][32];
    snprintf(summary_values[0], sizeof(summary_values[0]), "%u PPM", latest_ppm);
    snprintf(summary_values[1], sizeof(summary_values[1]), "%u PPM", latest_ppm);
    snprintf(summary_values[2], sizeof(summary_values[2]), "%s", readingLevelLabel(evaluation.level));
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *card = lv_obj_create(summary); lv_obj_remove_style_all(card); lv_obj_set_size(card, 220, 54);
        lv_obj_align(card, LV_ALIGN_LEFT_MID, 10 + i * 230, 0); set_panel_style(card, lv_color_hex(i == 2 ? 0xEEE8FF : 0xFFFFFF), 12);
        lv_obj_t *title = lv_label_create(card); lv_label_set_text(title, summary_titles[i]); lv_obj_set_style_text_font(title, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(title, lv_color_hex(MUTED), 0); lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 4);
        lv_obj_t *value = lv_label_create(card); lv_label_set_text(value, summary_values[i]); lv_obj_set_style_text_font(value, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(value, lv_color_hex(INK), 0); lv_obj_align(value, LV_ALIGN_BOTTOM_LEFT, 12, -4);
    }
    lv_obj_t *chart = make_content_panel(screen, 50, 244, 700, 164, 0xF7FBFF);
    create_fluency_legend_item(chart, 12, 0xD9534F, "Apoyo");
    create_fluency_legend_item(chart, 170, 0xD9A620, "Cerca del estándar");
    create_fluency_legend_item(chart, 390, GREEN, "Estándar");
    create_fluency_legend_item(chart, 520, BLUE, "Avanzado");
    uint16_t chart_max = 120;
    for (size_t i = 0; i < record_count; ++i) if (records[i].applied && records[i].ppm > chart_max) chart_max = records[i].ppm;
    const lv_coord_t plot_base = 140;
    const lv_coord_t plot_height = 86;
    const lv_coord_t bar_width = 82;
    for (size_t i = 0; i < record_count; ++i) {
        if (!records[i].applied) continue;
        const lv_coord_t bar_height = static_cast<lv_coord_t>((static_cast<uint32_t>(records[i].ppm) * plot_height) / chart_max);
        const ReadingFluencyEvaluation record_evaluation = evaluateReadingFluency(selected_student->grade, records[i].ppm);
        const lv_coord_t x = 96 + static_cast<lv_coord_t>(i * 210);
        lv_obj_t *bar = lv_obj_create(chart); lv_obj_remove_style_all(bar); lv_obj_set_size(bar, bar_width, bar_height > 4 ? bar_height : 4); set_panel_style(bar, fluency_level_color(record_evaluation.level), 10); lv_obj_align(bar, LV_ALIGN_TOP_LEFT, x, plot_base - (bar_height > 4 ? bar_height : 4));
        char value_text[12]; snprintf(value_text, sizeof(value_text), "%u", records[i].ppm);
        lv_obj_t *value = lv_label_create(chart); lv_label_set_text(value, value_text); lv_obj_set_style_text_font(value, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(value, lv_color_hex(INK), 0); lv_obj_set_width(value, bar_width); lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(value, LV_ALIGN_TOP_LEFT, x, plot_base - bar_height - 22);
        char date_text[12]; format_record_date_short(records[i].date, date_text, sizeof(date_text));
        lv_obj_t *date = lv_label_create(chart); lv_label_set_text(date, date_text); lv_obj_set_style_text_font(date, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(date, lv_color_hex(MUTED), 0); lv_obj_set_width(date, bar_width); lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(date, LV_ALIGN_TOP_LEFT, x, plot_base + 5);
    }
    create_back_button(screen);
}

void create_dictation_screen(lv_obj_t *screen)
{
    create_student_topbar(screen, "Dictado de oraciones", false, false); create_month_header(screen);
    size_t record_count = 0; const WritingRecord *records = getWritingRecords(selected_student->student_id, &record_count);
    const WritingRecord *latest = nullptr;
    const WritingRecord *best = nullptr;
    for (size_t i = 0; i < record_count; ++i) {
        if (records[i].applied) {
            latest = &records[i];
            if (best == nullptr || records[i].errors < best->errors) best = &records[i];
        }
    }

    lv_obj_t *panel = make_content_panel(screen, 40, 168, 720, 72, 0xFFF4D8);
    const char *summary_titles[] = {"ÚLTIMO", "PALABRAS", "MEJOR"};
    char summary_values[3][40];
    snprintf(summary_values[0], sizeof(summary_values[0]), "%s  ·  %d errores", latest ? latest->date : "Sin registros", latest ? latest->errors : 0);
    snprintf(summary_values[1], sizeof(summary_values[1]), "%u", latest ? latest->total_words : 0);
    snprintf(summary_values[2], sizeof(summary_values[2]), "%d errores  ·  %s", best ? best->errors : 0, best ? best->date : "Sin registros");
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *card = lv_obj_create(panel); lv_obj_remove_style_all(card); lv_obj_set_size(card, 220, 62);
        lv_obj_align(card, LV_ALIGN_LEFT_MID, 10 + i * 230, 0); set_panel_style(card, lv_color_hex(0xFFFFFF), 12);
        lv_obj_t *title = lv_label_create(card); lv_label_set_text(title, summary_titles[i]); lv_obj_set_style_text_font(title, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(title, lv_color_hex(MUTED), 0); lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 4);
        lv_obj_t *value = lv_label_create(card); lv_label_set_text(value, summary_values[i]); lv_obj_set_style_text_font(value, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(value, lv_color_hex(INK), 0); lv_obj_set_width(value, 194); lv_label_set_long_mode(value, LV_LABEL_LONG_DOT); lv_obj_align(value, LV_ALIGN_BOTTOM_LEFT, 12, -4);
    }

    lv_obj_t *chart = make_content_panel(screen, 40, 252, 720, 156, 0xF7FBFF);
    lv_obj_t *chart_hint = lv_label_create(chart); lv_label_set_text(chart_hint, "Errores · menos es mejor"); lv_obj_set_style_text_font(chart_hint, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(chart_hint, lv_color_hex(MUTED), 0); lv_obj_align(chart_hint, LV_ALIGN_TOP_LEFT, 18, 8);
    uint16_t chart_max = 10;
    for (size_t i = 0; i < record_count; ++i) if (records[i].applied && records[i].errors > chart_max) chart_max = records[i].errors;
    const lv_coord_t plot_base = 124;
    const lv_coord_t plot_height = 76;
    const lv_coord_t date_x[] = {64, 240, 416, 592};
    for (size_t i = 0; i < record_count; ++i) {
        if (records[i].applied) {
            const lv_coord_t bar_height = static_cast<lv_coord_t>((static_cast<uint32_t>(records[i].errors) * plot_height) / chart_max);
            lv_obj_t *bar = lv_obj_create(chart); lv_obj_remove_style_all(bar); lv_obj_set_size(bar, 70, bar_height > 4 ? bar_height : 4); set_panel_style(bar, lv_color_hex(ORANGE), 10); lv_obj_align(bar, LV_ALIGN_TOP_LEFT, date_x[i], plot_base - (bar_height > 4 ? bar_height : 4));
            char value_text[8]; snprintf(value_text, sizeof(value_text), "%d", records[i].errors);
            lv_obj_t *value = lv_label_create(chart); lv_label_set_text(value, value_text); lv_obj_set_style_text_font(value, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(value, lv_color_hex(INK), 0); lv_obj_set_width(value, 70); lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(value, LV_ALIGN_TOP_LEFT, date_x[i], plot_base - bar_height - 22);
        } else {
            lv_obj_t *na = lv_label_create(chart); lv_label_set_text(na, "N/A"); lv_obj_set_style_text_font(na, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(na, lv_color_hex(PURPLE), 0); lv_obj_set_width(na, 70); lv_obj_set_style_text_align(na, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(na, LV_ALIGN_TOP_LEFT, date_x[i], plot_base - 22);
        }
        char date_text[12]; format_record_date_short(records[i].date, date_text, sizeof(date_text));
        lv_obj_t *date = lv_label_create(chart); lv_label_set_text(date, date_text); lv_obj_set_style_text_font(date, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(date, lv_color_hex(MUTED), 0); lv_obj_set_width(date, 70); lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(date, LV_ALIGN_TOP_LEFT, date_x[i], plot_base + 5);
    }
    create_back_button(screen);
}

void create_goals_screen(lv_obj_t *screen)
{
    create_student_topbar(screen, "Mis metas", false, false);
    lv_obj_t *panel = make_content_panel(screen, 60, 120, 680, 270, 0xFBEAF2);
    set_content_title(panel, "Fluidez lectora");
    size_t record_count = 0; const ReadingRecord *records = getReadingRecords(selected_student->student_id, &record_count);
    const uint16_t latest_ppm = records[record_count - 1].ppm;
    const ReadingFluencyEvaluation evaluation = evaluateReadingFluency(selected_student->grade, latest_ppm);
    char current_text[48]; snprintf(current_text, sizeof(current_text), "Nivel actual:\n%s", readingLevelLabel(evaluation.level));
    lv_obj_t *current = lv_label_create(panel); lv_label_set_text(current, current_text); lv_obj_set_style_text_font(current, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(current, lv_color_hex(INK), 0); lv_obj_align(current, LV_ALIGN_TOP_LEFT, 28, 68);
    char ppm_text[24]; snprintf(ppm_text, sizeof(ppm_text), "%u PPM", latest_ppm);
    lv_obj_t *ppm = lv_label_create(panel); lv_label_set_text(ppm, ppm_text); lv_obj_set_style_text_font(ppm, &lv_font_montserrat_30, 0); lv_obj_set_style_text_color(ppm, lv_color_hex(PURPLE), 0); lv_obj_align(ppm, LV_ALIGN_TOP_RIGHT, -34, 62);
    lv_obj_t *last = lv_label_create(panel); lv_label_set_text(last, "Última medición"); lv_obj_set_style_text_font(last, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(last, lv_color_hex(MUTED), 0); lv_obj_align(last, LV_ALIGN_TOP_RIGHT, -38, 102);
    if (evaluation.has_next_level) {
        char goal_text[80]; snprintf(goal_text, sizeof(goal_text), "Meta: %u PPM\nSiguiente nivel: %s\nTe faltan: %u PPM", evaluation.next_level_ppm, nextReadingLevelLabel(evaluation.level), evaluation.ppm_to_next_level);
        lv_obj_t *goal = lv_label_create(panel); lv_label_set_text(goal, goal_text); lv_obj_set_style_text_font(goal, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(goal, lv_color_hex(INK), 0); lv_obj_align(goal, LV_ALIGN_TOP_LEFT, 28, 140);
        const uint16_t range = evaluation.next_level_ppm > evaluation.level_lower_bound ? evaluation.next_level_ppm - evaluation.level_lower_bound : 1;
        const uint16_t progressed = latest_ppm > evaluation.level_lower_bound ? latest_ppm - evaluation.level_lower_bound : 0;
        const uint8_t progress = static_cast<uint8_t>(progressed >= range ? 100 : (progressed * 100) / range);
        lv_obj_t *bar = lv_bar_create(panel); lv_obj_set_size(bar, 410, 18); lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 28, -28); lv_bar_set_range(bar, 0, 100); lv_bar_set_value(bar, progress, LV_ANIM_OFF); lv_obj_set_style_bg_color(bar, lv_color_hex(0xE3D8F7), LV_PART_MAIN); lv_obj_set_style_bg_color(bar, lv_color_hex(PURPLE), LV_PART_INDICATOR);
    } else {
        lv_obj_t *goal = lv_label_create(panel); lv_label_set_text(goal, "¡Alcanzaste el nivel Avanzado!"); lv_obj_set_style_text_font(goal, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(goal, lv_color_hex(INK), 0); lv_obj_align(goal, LV_ALIGN_TOP_LEFT, 28, 150);
    }
    create_back_button(screen);
}

void create_achievements_screen(lv_obj_t *screen)
{
    create_student_topbar(screen, "Logros", false, false);
    lv_obj_t *panel = make_content_panel(screen, 90, 136, 620, 218, 0xEEE8FF);
    lv_obj_t *badge = make_shape(panel, 72, 72, PURPLE); lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 34, 44);
    lv_obj_t *label = lv_label_create(panel); lv_label_set_text(label, "Logro de demostración"); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0); lv_obj_align(label, LV_ALIGN_TOP_LEFT, 140, 48);
    label = lv_label_create(panel); lv_label_set_text(label, "Diseño preparado para futuros logros\nSin reglas automáticas todavía"); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(MUTED), 0); lv_obj_align(label, LV_ALIGN_TOP_LEFT, 140, 100);
    create_back_button(screen);
}

lv_obj_t *create_transfer_button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height,
                                 const char *text, lv_event_cb_t callback, intptr_t user_data = 0, bool enabled = true)
{
    lv_obj_t *button = lv_obj_create(parent); lv_obj_remove_style_all(button); lv_obj_set_size(button, width, height); lv_obj_set_pos(button, x, y);
    apply_arcade_button_style(button, 0xE8F0FF, enabled ? BLUE : MUTED, enabled);
    lv_obj_t *label = lv_label_create(button); lv_label_set_text(label, text); const bool is_single_digit = text[0] >= '0' && text[0] <= '9' && text[1] == '\0'; lv_obj_set_style_text_font(label, is_single_digit ? &lv_font_montserrat_30 : &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, enabled ? lv_color_hex(INK) : lv_color_hex(MUTED), 0); lv_obj_set_width(label, width - 12); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(label);
    if (enabled) { lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(button, arcade_button_feedback_event, LV_EVENT_ALL, nullptr); lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, reinterpret_cast<void *>(user_data)); }
    return button;
}

void create_transfer_profile(lv_obj_t *parent, lv_coord_t x, const char *role, const Student *student, uint32_t color)
{
    lv_obj_t *profile = lv_obj_create(parent); lv_obj_remove_style_all(profile); lv_obj_set_size(profile, 300, 76); lv_obj_set_pos(profile, x, 18); apply_arcade_badge_style(profile, color, color == 0xDDF3FF ? BLUE : GREEN);
    lv_obj_t *role_label = lv_label_create(profile); lv_label_set_text(role_label, role); lv_obj_set_style_text_font(role_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(role_label, lv_color_hex(MUTED), 0); lv_obj_align(role_label, LV_ALIGN_TOP_LEFT, 16, 7);
    lv_obj_t *name = lv_label_create(profile); lv_label_set_text(name, student_short_name(student)); lv_obj_set_style_text_font(name, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(name, lv_color_hex(INK), 0); lv_obj_align(name, LV_ALIGN_TOP_LEFT, 16, 30);
    char grade[16]; snprintf(grade, sizeof(grade), "%u.\xC2\xBA %s", student ? student->grade : 0, student && student->group ? student->group : "");
    lv_obj_t *grade_label = lv_label_create(profile); lv_label_set_text(grade_label, grade); lv_obj_set_style_text_font(grade_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(grade_label, lv_color_hex(PURPLE), 0); lv_obj_align(grade_label, LV_ALIGN_TOP_RIGHT, -16, 30);
}

void transfer_clear_state()
{
    transfer_sender_id = 0; transfer_receiver_id = 0; transfer_amount = 0; transfer_receiver_detected = false; transfer_balance_warning = false; transfer_keypad_buffer[0] = '\0';
}

void transfer_set_amount(int32_t delta)
{
    const uint32_t balance = getStudentBalance() > 0 ? static_cast<uint32_t>(getStudentBalance()) : 0;
    int64_t next = static_cast<int64_t>(transfer_amount) + delta;
    if (next < 0) next = 0;
    if (next > static_cast<int64_t>(balance)) {
        next = balance;
        transfer_balance_warning = true;
    } else if (transfer_feedback_label) {
        transfer_balance_warning = false;
    }
    transfer_amount = static_cast<uint32_t>(next);
}

void transfer_receiver_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const Student *receiver = getStudentById(13);
    if (receiver == nullptr || selected_student == nullptr || receiver->student_id == selected_student->student_id) return;
    transfer_sender_id = selected_student->student_id; transfer_receiver_id = receiver->student_id; transfer_receiver_detected = true; transfer_amount = 0; transfer_keypad_buffer[0] = '\0';
    register_student_activity(); mostrar_pantalla(PANTALLA_TRANSFERIR);
}

void transfer_amount_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    register_student_activity(); transfer_set_amount(static_cast<int32_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event))));
    mostrar_pantalla(PANTALLA_TRANSFERIR);
}

void transfer_keypad_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    register_student_activity();
    const intptr_t action = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    if (action >= 0 && action <= 9) {
        const size_t length = strlen(transfer_keypad_buffer);
        if (length < sizeof(transfer_keypad_buffer) - 1) {
            transfer_keypad_buffer[length] = static_cast<char>('0' + action); transfer_keypad_buffer[length + 1] = '\0';
        }
        if (transfer_keypad_input_label) lv_label_set_text(transfer_keypad_input_label, transfer_keypad_buffer[0] ? transfer_keypad_buffer : "0");
        return;
    }
    if (action == -1) { transfer_keypad_buffer[0] = '\0'; if (transfer_keypad_input_label) lv_label_set_text(transfer_keypad_input_label, "0"); return; }
    if (action == -2) {
        const uint32_t value = transfer_keypad_buffer[0] ? static_cast<uint32_t>(strtoul(transfer_keypad_buffer, nullptr, 10)) : 0;
        const uint32_t balance = getStudentBalance() > 0 ? static_cast<uint32_t>(getStudentBalance()) : 0;
        if (value > balance) {
            transfer_balance_warning = true;
            mostrar_pantalla(PANTALLA_TRANSFER_TECLADO);
            return;
        }
        transfer_amount = value; transfer_balance_warning = false; transfer_keypad_buffer[0] = '\0'; mostrar_pantalla(PANTALLA_TRANSFERIR); return;
    }
    if (action == -4) { transfer_keypad_buffer[0] = '\0'; transfer_balance_warning = false; mostrar_pantalla(PANTALLA_TRANSFER_TECLADO); return; }
    transfer_keypad_buffer[0] = '\0'; mostrar_pantalla(PANTALLA_TRANSFERIR);
}

void transfer_continue_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (transfer_receiver_detected && transfer_amount > 0) { register_student_activity(); mostrar_pantalla(PANTALLA_TRANSFER_CONFIRMAR); }
}

void transfer_confirm_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    register_student_activity(); mostrar_pantalla(PANTALLA_TRANSFER_RESULTADO);
}

void transfer_cancel_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    register_student_activity();
    if (pantalla_actual == PANTALLA_TRANSFER_CONFIRMAR) mostrar_pantalla(PANTALLA_TRANSFERIR);
    else if (pantalla_actual == PANTALLA_TRANSFER_TECLADO) { transfer_keypad_buffer[0] = '\0'; mostrar_pantalla(PANTALLA_TRANSFERIR); }
    else { transfer_clear_state(); mostrar_pantalla(PANTALLA_ALUMNO); }
}

void create_arcade_transfer_screen(lv_obj_t *screen)
{
    char transfer_title[48]; snprintf(transfer_title, sizeof(transfer_title), "Transferir %s", CURRENCY_NAME);
    create_student_topbar(screen, transfer_title, false, false);
    const Student *sender = getStudentById(transfer_sender_id ? transfer_sender_id : (selected_student ? selected_student->student_id : 0));
    const Student *receiver = getStudentById(transfer_receiver_id);
    lv_obj_t *panel = make_content_panel(screen, GRID_MARGIN, 112, SCREEN_WIDTH - (GRID_MARGIN * 2), 282, 0xF7FBFF);
    if (!transfer_receiver_detected || receiver == nullptr) {
        lv_obj_t *title = lv_label_create(panel); lv_label_set_text(title, "TU SALDO"); lv_obj_set_style_text_font(title, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(title, lv_color_hex(MUTED), 0); lv_obj_align(title, LV_ALIGN_TOP_LEFT, 28, 20);
        char balance_text[40]; snprintf(balance_text, sizeof(balance_text), "%ld %s", static_cast<long>(getStudentBalance()), CURRENCY_NAME);
        lv_obj_t *balance = lv_label_create(panel); lv_label_set_text(balance, balance_text); lv_obj_set_style_text_font(balance, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(balance, lv_color_hex(ORANGE), 0); lv_obj_align(balance, LV_ALIGN_TOP_LEFT, 28, 50);
        lv_obj_t *icon = create_card_icon(panel); lv_obj_align(icon, LV_ALIGN_LEFT_MID, 48, 20);
        lv_obj_t *hint = lv_label_create(panel); lv_label_set_text(hint, "ACERCA LA TARJETA DEL COMPAÑERO"); lv_obj_set_style_text_font(hint, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(hint, lv_color_hex(INK), 0); lv_obj_set_width(hint, 420); lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 300, 106);
        create_transfer_button(panel, 300, 164, 420, 48, "SIMULAR TARJETA RECEPTOR", transfer_receiver_event);
    } else {
        create_transfer_profile(panel, 46, "DE", sender, 0xDDF3FF);
        create_transfer_profile(panel, 418, "PARA", receiver, 0xE4F7EC);
        lv_obj_t *amount_title = lv_label_create(panel); lv_label_set_text(amount_title, "CANTIDAD A TRANSFERIR"); lv_obj_set_style_text_font(amount_title, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(amount_title, lv_color_hex(MUTED), 0); lv_obj_align(amount_title, LV_ALIGN_TOP_LEFT, 46, 108);
        char amount_text[40]; snprintf(amount_text, sizeof(amount_text), "%lu %s", static_cast<unsigned long>(transfer_amount), CURRENCY_NAME);
        lv_obj_t *amount = lv_label_create(panel); lv_label_set_text(amount, amount_text); lv_obj_set_style_text_font(amount, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(amount, lv_color_hex(PURPLE), 0); lv_obj_set_width(amount, 300); lv_obj_align(amount, LV_ALIGN_TOP_LEFT, 46, 132);
        create_transfer_button(panel, 46, 184, 100, 40, "+5", transfer_amount_event, 5);
        create_transfer_button(panel, 158, 184, 100, 40, "+10", transfer_amount_event, 10);
        create_transfer_button(panel, 270, 184, 100, 40, "-5", transfer_amount_event, -5);
        create_transfer_button(panel, 382, 184, 100, 40, "-10", transfer_amount_event, -10);
        create_transfer_button(panel, 46, 234, 210, 40, "OTRA CANTIDAD", transfer_keypad_event, -4);
        const bool can_continue = transfer_amount > 0;
        create_transfer_button(panel, 482, 234, 180, 40, "CONTINUAR", transfer_continue_event, 0, can_continue);
        transfer_feedback_label = lv_label_create(panel); lv_label_set_text(transfer_feedback_label, transfer_balance_warning ? "No puedes transferir más de tu saldo." : ""); lv_obj_set_style_text_font(transfer_feedback_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(transfer_feedback_label, lv_color_hex(ORANGE), 0); lv_obj_set_width(transfer_feedback_label, 420); lv_obj_align(transfer_feedback_label, LV_ALIGN_TOP_LEFT, 46, 162);
    }
    create_back_button(screen);
}

void create_transfer_screen(lv_obj_t *screen)
{
    create_arcade_transfer_screen(screen);
    return;
    char transfer_title[48]; snprintf(transfer_title, sizeof(transfer_title), "Transferir %s", CURRENCY_NAME);
    create_student_topbar(screen, transfer_title, false, false);
    const Student *sender = getStudentById(transfer_sender_id ? transfer_sender_id : (selected_student ? selected_student->student_id : 0));
    const Student *receiver = getStudentById(transfer_receiver_id);
    if (!transfer_receiver_detected || receiver == nullptr) {
        lv_obj_t *panel = make_content_panel(screen, 40, 120, 720, 276, 0xF7FBFF);
        lv_obj_t *title = lv_label_create(panel); lv_label_set_text(title, "Tu saldo"); lv_obj_set_style_text_font(title, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(title, lv_color_hex(MUTED), 0); lv_obj_align(title, LV_ALIGN_TOP_LEFT, 28, 22);
        char balance_text[40]; snprintf(balance_text, sizeof(balance_text), "%ld %s", static_cast<long>(getStudentBalance()), CURRENCY_NAME);
        lv_obj_t *balance = lv_label_create(panel); lv_label_set_text(balance, balance_text); lv_obj_set_style_text_font(balance, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(balance, lv_color_hex(INK), 0); lv_obj_align(balance, LV_ALIGN_TOP_LEFT, 28, 54);
        lv_obj_t *icon = make_shape(panel, 108, 72, 0xDDF3FF, 16); lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 44, 112); lv_obj_t *stripe = make_shape(icon, 108, 10, BLUE, 0); lv_obj_align(stripe, LV_ALIGN_TOP_MID, 0, 15); lv_obj_t *icon_text = lv_label_create(icon); lv_label_set_text(icon_text, "NFC"); lv_obj_set_style_text_font(icon_text, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(icon_text, lv_color_hex(INK), 0); lv_obj_center(icon_text);
        lv_obj_t *hint = lv_label_create(panel); lv_label_set_text(hint, "Acerca la tarjeta del compañero"); lv_obj_set_style_text_font(hint, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(hint, lv_color_hex(INK), 0); lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 230, 116);
        create_transfer_button(panel, 230, 164, 430, 44, "SIMULAR TARJETA RECEPTOR", transfer_receiver_event);
    } else {
        lv_obj_t *panel = make_content_panel(screen, 40, 120, 720, 276, 0xF7FBFF);
        create_transfer_profile(panel, 46, "DE", sender, 0xDDF3FF); create_transfer_profile(panel, 374, "PARA", receiver, 0xE4F7EC);
        lv_obj_t *amount_title = lv_label_create(panel); lv_label_set_text(amount_title, "CANTIDAD A TRANSFERIR"); lv_obj_set_style_text_font(amount_title, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(amount_title, lv_color_hex(MUTED), 0); lv_obj_align(amount_title, LV_ALIGN_TOP_LEFT, 46, 108);
        char amount_text[40]; snprintf(amount_text, sizeof(amount_text), "%lu %s", static_cast<unsigned long>(transfer_amount), CURRENCY_NAME);
        lv_obj_t *amount = lv_label_create(panel); lv_label_set_text(amount, amount_text); lv_obj_set_style_text_font(amount, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(amount, lv_color_hex(PURPLE), 0); lv_obj_set_width(amount, 300); lv_obj_set_style_text_align(amount, LV_TEXT_ALIGN_LEFT, 0); lv_obj_align(amount, LV_ALIGN_TOP_LEFT, 46, 132);
        create_transfer_button(panel, 46, 184, 100, 38, "+5", transfer_amount_event, 5); create_transfer_button(panel, 158, 184, 100, 38, "+10", transfer_amount_event, 10); create_transfer_button(panel, 270, 184, 100, 38, "-5", transfer_amount_event, -5); create_transfer_button(panel, 382, 184, 100, 38, "-10", transfer_amount_event, -10);
        create_transfer_button(panel, 46, 228, 210, 38, "OTRA CANTIDAD", transfer_keypad_event, -4);
        const bool can_continue = transfer_amount > 0;
        create_transfer_button(panel, 482, 228, 180, 38, "CONTINUAR", transfer_continue_event, 0, can_continue);
        transfer_feedback_label = lv_label_create(panel); lv_label_set_text(transfer_feedback_label, transfer_balance_warning ? "No puedes transferir más de tu saldo." : ""); lv_obj_set_style_text_font(transfer_feedback_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(transfer_feedback_label, lv_color_hex(ORANGE), 0); lv_obj_set_width(transfer_feedback_label, 420); lv_obj_align(transfer_feedback_label, LV_ALIGN_TOP_LEFT, 46, 164);
    }
    create_back_button(screen);
}

void create_arcade_transfer_keypad_screen(lv_obj_t *screen)
{
    create_student_topbar(screen, "Otra cantidad", false, false);
    lv_obj_t *panel = make_content_panel(screen, 18, 108, 764, 300, 0xF7FBFF);
    transfer_keypad_input_label = lv_label_create(panel); lv_label_set_text(transfer_keypad_input_label, transfer_keypad_buffer[0] ? transfer_keypad_buffer : "0"); lv_obj_set_style_text_font(transfer_keypad_input_label, &lv_font_montserrat_30, 0); lv_obj_set_style_text_color(transfer_keypad_input_label, lv_color_hex(PURPLE), 0); lv_obj_set_width(transfer_keypad_input_label, 740); lv_obj_set_style_text_align(transfer_keypad_input_label, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(transfer_keypad_input_label, LV_ALIGN_TOP_MID, 0, 6);
    transfer_feedback_label = lv_label_create(panel); lv_label_set_text(transfer_feedback_label, transfer_balance_warning ? "No puedes transferir más de tu saldo." : ""); lv_obj_set_style_text_font(transfer_feedback_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(transfer_feedback_label, lv_color_hex(ORANGE), 0); lv_obj_set_width(transfer_feedback_label, 740); lv_obj_set_style_text_align(transfer_feedback_label, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(transfer_feedback_label, LV_ALIGN_TOP_MID, 0, 48);
    const char *digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
    for (int i = 0; i < 10; ++i) {
        const int row = i < 9 ? i / 3 : 3;
        const int col = i < 9 ? i % 3 : 1;
        const int x = 47 + col * 115;
        const int y = 76 + row * 52;
        create_transfer_button(panel, x, y, 105, 46, digits[i], transfer_keypad_event, i + 1 == 10 ? 0 : i + 1);
    }
    create_transfer_button(panel, 450, 232, 95, 46, "BORRAR", transfer_keypad_event, -1);
    create_transfer_button(panel, 554, 232, 95, 46, "ACEPTAR", transfer_keypad_event, -2);
    create_transfer_button(panel, 658, 232, 95, 46, "CANCELAR", transfer_keypad_event, -3);
}

void create_transfer_keypad_screen(lv_obj_t *screen)
{
    create_arcade_transfer_keypad_screen(screen);
    return;
    create_student_topbar(screen, "Otra cantidad", false, false);
    lv_obj_t *panel = make_content_panel(screen, 100, 112, 600, 300, 0xF7FBFF);
    transfer_keypad_input_label = lv_label_create(panel); lv_label_set_text(transfer_keypad_input_label, transfer_keypad_buffer[0] ? transfer_keypad_buffer : "0"); lv_obj_set_style_text_font(transfer_keypad_input_label, &lv_font_montserrat_30, 0); lv_obj_set_style_text_color(transfer_keypad_input_label, lv_color_hex(PURPLE), 0); lv_obj_set_width(transfer_keypad_input_label, 560); lv_obj_set_style_text_align(transfer_keypad_input_label, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(transfer_keypad_input_label, LV_ALIGN_TOP_MID, 0, 16);
    transfer_feedback_label = lv_label_create(panel); lv_label_set_text(transfer_feedback_label, transfer_balance_warning ? "No puedes transferir más de tu saldo." : ""); lv_obj_set_style_text_font(transfer_feedback_label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(transfer_feedback_label, lv_color_hex(ORANGE), 0); lv_obj_set_width(transfer_feedback_label, 560); lv_obj_set_style_text_align(transfer_feedback_label, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(transfer_feedback_label, LV_ALIGN_TOP_MID, 0, 204);
    const char *digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
    for (int i = 0; i < 10; ++i) { const int row = i < 9 ? i / 3 : 3; const int col = i < 9 ? i % 3 : 1; create_transfer_button(panel, 150 + col * 82, 62 + row * 42, 68, 34, digits[i], transfer_keypad_event, i + 1 == 10 ? 0 : i + 1); }
    create_transfer_button(panel, 70, 236, 130, 38, "BORRAR", transfer_keypad_event, -1); create_transfer_button(panel, 235, 236, 130, 38, "ACEPTAR", transfer_keypad_event, -2); create_transfer_button(panel, 400, 236, 130, 38, "CANCELAR", transfer_keypad_event, -3);
}

void create_arcade_transfer_confirm_screen(lv_obj_t *screen)
{
    create_student_topbar(screen, "Confirmar transferencia", false, false);
    const Student *sender = getStudentById(transfer_sender_id); const Student *receiver = getStudentById(transfer_receiver_id);
    lv_obj_t *panel = make_content_panel(screen, 118, 112, 564, 282, 0xFFF4D8);
    lv_obj_t *title = lv_label_create(panel); lv_label_set_text(title, "CONFIRMAR OPERACIÓN"); lv_obj_set_style_text_font(title, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(title, lv_color_hex(ORANGE), 0); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_t *from = lv_label_create(panel); char from_text[64]; snprintf(from_text, sizeof(from_text), "De: %s", student_short_name(sender)); lv_label_set_text(from, from_text); lv_obj_set_style_text_font(from, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(from, lv_color_hex(INK), 0); lv_obj_align(from, LV_ALIGN_TOP_LEFT, 52, 66);
    lv_obj_t *arrow = lv_label_create(panel); lv_label_set_text(arrow, "TRANSFERIR"); lv_obj_set_style_text_font(arrow, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(arrow, lv_color_hex(PURPLE), 0); lv_obj_align(arrow, LV_ALIGN_TOP_MID, 0, 66);
    lv_obj_t *to = lv_label_create(panel); char to_text[64]; snprintf(to_text, sizeof(to_text), "Para: %s", student_short_name(receiver)); lv_label_set_text(to, to_text); lv_obj_set_style_text_font(to, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(to, lv_color_hex(INK), 0); lv_obj_align(to, LV_ALIGN_TOP_RIGHT, -52, 66);
    char text[160]; snprintf(text, sizeof(text), "Cantidad: %lu %s\nSaldo actual: %ld %s\nSaldo después: %ld %s", static_cast<unsigned long>(transfer_amount), CURRENCY_NAME, static_cast<long>(getStudentBalance()), CURRENCY_NAME, static_cast<long>(getStudentBalance() - transfer_amount), CURRENCY_NAME);
    lv_obj_t *details = lv_label_create(panel); lv_label_set_text(details, text); lv_obj_set_style_text_font(details, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(details, lv_color_hex(INK), 0); lv_obj_set_style_text_align(details, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_width(details, 480); lv_obj_align(details, LV_ALIGN_TOP_MID, 0, 116);
    create_transfer_button(panel, 72, 222, 190, 42, "CONFIRMAR", transfer_confirm_event, 1);
    create_transfer_button(panel, 302, 222, 190, 42, "CANCELAR", transfer_cancel_event, 0);
    create_back_button(screen);
}

void create_transfer_confirm_screen(lv_obj_t *screen)
{
    create_arcade_transfer_confirm_screen(screen);
    return;
    create_student_topbar(screen, "Confirmar transferencia", false, false);
    const Student *sender = getStudentById(transfer_sender_id); const Student *receiver = getStudentById(transfer_receiver_id);
    lv_obj_t *panel = make_content_panel(screen, 90, 116, 620, 292, 0xFFF4D8);
    char text[240]; snprintf(text, sizeof(text), "De: %s\nPara: %s\n\nCantidad: %lu %s\nSaldo actual: %ld %s\nSaldo después: %ld %s", student_short_name(sender), student_short_name(receiver), static_cast<unsigned long>(transfer_amount), CURRENCY_NAME, static_cast<long>(getStudentBalance()), CURRENCY_NAME, static_cast<long>(getStudentBalance() - transfer_amount), CURRENCY_NAME);
    lv_obj_t *label = lv_label_create(panel); lv_label_set_text(label, text); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0); lv_obj_align(label, LV_ALIGN_TOP_LEFT, 34, 28);
    create_transfer_button(panel, 86, 222, 190, 42, "CONFIRMAR", transfer_confirm_event, 1); create_transfer_button(panel, 344, 222, 190, 42, "CANCELAR", transfer_cancel_event, 0);
}

void create_arcade_transfer_result_screen(lv_obj_t *screen)
{
    create_student_topbar(screen, "Transferencia demo", false, false);
    const Student *sender = getStudentById(transfer_sender_id); const Student *receiver = getStudentById(transfer_receiver_id);
    lv_obj_t *panel = make_content_panel(screen, 150, 126, 500, 246, 0xE4F7EC);
    lv_obj_t *title = lv_label_create(panel); lv_label_set_text(title, "TRANSFERENCIA DEMO"); lv_obj_set_style_text_font(title, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(title, lv_color_hex(GREEN), 0); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);
    char text[160]; snprintf(text, sizeof(text), "%s  ->  %s\n%lu %s\n\nSimulación completada", student_short_name(sender), student_short_name(receiver), static_cast<unsigned long>(transfer_amount), CURRENCY_NAME);
    lv_obj_t *label = lv_label_create(panel); lv_label_set_text(label, text); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_width(label, 440); lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 72);
    create_transfer_button(panel, 120, 178, 260, 42, "VOLVER AL INICIO", transfer_cancel_event, 0);
}

void create_transfer_result_screen(lv_obj_t *screen)
{
    create_arcade_transfer_result_screen(screen);
    return;
    create_student_topbar(screen, "Transferencia demo", false, false);
    const Student *sender = getStudentById(transfer_sender_id); const Student *receiver = getStudentById(transfer_receiver_id);
    lv_obj_t *panel = make_content_panel(screen, 120, 132, 560, 220, 0xE4F7EC);
    lv_obj_t *title = lv_label_create(panel); lv_label_set_text(title, "TRANSFERENCIA DEMO"); lv_obj_set_style_text_font(title, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(title, lv_color_hex(GREEN), 0); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);
    char text[120]; snprintf(text, sizeof(text), "%s  ->  %s\n%lu %s\n\nSimulación completada", student_short_name(sender), student_short_name(receiver), static_cast<unsigned long>(transfer_amount), CURRENCY_NAME);
    lv_obj_t *label = lv_label_create(panel); lv_label_set_text(label, text); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_width(label, 480); lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 74);
    create_transfer_button(panel, 150, 164, 260, 42, "VOLVER AL INICIO", transfer_cancel_event, 0);
}

void demo_detection_timer_cb(lv_timer_t *timer)
{
    lv_timer_del(timer); demo_detection_timer = nullptr; register_student_activity(); mostrar_pantalla(PANTALLA_ALUMNO);
}

void create_detection_screen(lv_obj_t *screen)
{
    lv_obj_t *panel = lv_obj_create(screen); lv_obj_remove_style_all(panel); lv_obj_set_size(panel, 520, 280); set_panel_style(panel, lv_color_hex(0xE8F7FF), 30); lv_obj_center(panel);
    lv_obj_t *ring = make_shape(panel, 108, 108, SKY); lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(ring, 6, 0); lv_obj_set_style_border_color(ring, lv_color_hex(BLUE), 0); lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, 28);
    char greeting[96]; snprintf(greeting, sizeof(greeting), "\xC2\xA1" "Buenos d\xC3\xAD" "as, %s!", selected_student->preferred_name ? selected_student->preferred_name : selected_student->name);
    lv_obj_t *label = lv_label_create(panel); lv_label_set_text(label, greeting); lv_obj_set_style_text_font(label, &banco_escolar_font_16, 0); lv_obj_set_style_text_color(label, lv_color_hex(INK), 0); lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -72);
    label = lv_label_create(panel); lv_label_set_text(label, "Entrada registrada"); lv_obj_set_style_text_color(label, lv_color_hex(GREEN), 0); lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -42);
    char date_text[48];
    char time_text[16];
    obtener_texto_fecha_hora(date_text, sizeof(date_text), time_text, sizeof(time_text));
    label = lv_label_create(panel); lv_label_set_text(label, time_text); lv_obj_set_style_text_color(label, lv_color_hex(PURPLE), 0); lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -16);
    demo_detection_timer = lv_timer_create(demo_detection_timer_cb, 1000, nullptr);
}

bool is_back_navigation(Pantalla from, Pantalla to)
{
    if (to == PANTALLA_ALUMNO) {
        return from == PANTALLA_CUENTA || from == PANTALLA_PROGRESO ||
               from == PANTALLA_METAS || from == PANTALLA_LOGROS ||
               from == PANTALLA_TRANSFER_RESULTADO;
    }
    if (to == PANTALLA_PROGRESO) {
        return from == PANTALLA_FLUIDEZ || from == PANTALLA_DICTADO;
    }
    if (to == PANTALLA_TRANSFERIR) {
        return from == PANTALLA_TRANSFER_TECLADO || from == PANTALLA_TRANSFER_CONFIRMAR;
    }
    return false;
}

lv_scr_load_anim_t transition_animation_for(Pantalla from, Pantalla to)
{
    (void)from;
    (void)to;
    return LV_SCR_LOAD_ANIM_FADE_ON;
}

void transition_release_cb(lv_timer_t *timer)
{
    performance_transition_ms = millis() - performance_transition_started_ms;
    if (PERFORMANCE_TEST_ENABLED) {
        PerformanceScreenStats &stats = performance_stats[static_cast<uint8_t>(performance_transition_to)];
        stats.transition_ms = performance_transition_ms;
        Serial.printf("[Perf] Transition %s->%s %lums\n",
                      performance_screen_name(performance_transition_from),
                      performance_screen_name(performance_transition_to),
                      static_cast<unsigned long>(performance_transition_ms));
    }
    transition_in_progress = false;
    if (transition_release_timer == timer) transition_release_timer = nullptr;
    lv_timer_del(timer);
}

void mostrar_pantalla(Pantalla pantalla)
{
    if (transition_in_progress) return;
    const Pantalla previous_screen = pantalla_actual;
    transition_in_progress = true;
    const uint32_t screen_create_started_ms = millis();
    if (demo_detection_timer && pantalla != PANTALLA_DETECCION) { lv_timer_del(demo_detection_timer); demo_detection_timer = nullptr; }

    lv_obj_t *next_screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(next_screen);
    lv_obj_set_size(next_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(next_screen, theme_color_hex(theme_palette().background), 0);
    lv_obj_set_style_bg_opa(next_screen, LV_OPA_COVER, 0);

    pantalla_actual = pantalla;
    wifi_ui_needs_refresh = true;
    nfc_message_label = nullptr; nfc_hint_label = nullptr; nfc_status_label = nullptr; wifi_status_label = nullptr; wifi_config_state_label = nullptr; wifi_config_ssid_label = nullptr; wifi_config_ip_label = nullptr; wifi_config_rssi_label = nullptr; wifi_scan_status_label = nullptr; wifi_scan_list = nullptr; wifi_password_textarea = nullptr; wifi_manual_ssid_textarea = nullptr; wifi_keyboard = nullptr; wifi_connect_status_label = nullptr; motivation_label = nullptr; waiting_indicator = nullptr; nfc_panel = nullptr; clock_label = nullptr; date_label = nullptr; balance_amount_label = nullptr; balance_unit_label = nullptr; transfer_feedback_label = nullptr; transfer_keypad_input_label = nullptr;
    performance_overlay = nullptr; performance_label = nullptr;
    if (pantalla == PANTALLA_ALUMNO) register_student_activity();
    create_particle_background(next_screen, particle_intensity_for_screen(pantalla));
    switch (pantalla) {
        case PANTALLA_ESPERA: create_header(next_screen); create_nfc_area(next_screen); create_status_bar(next_screen); break;
        case PANTALLA_DETECCION: create_detection_screen(next_screen); break;
        case PANTALLA_ALUMNO: create_student_home(next_screen); break;
        case PANTALLA_CUENTA: create_account_screen(next_screen); break;
        case PANTALLA_PROGRESO: create_progress_screen(next_screen); break;
        case PANTALLA_FLUIDEZ: create_fluency_screen(next_screen); break;
        case PANTALLA_DICTADO: create_dictation_screen(next_screen); break;
        case PANTALLA_METAS: create_goals_screen(next_screen); break;
        case PANTALLA_LOGROS: create_achievements_screen(next_screen); break;
        case PANTALLA_ENTRADA: create_detection_screen(next_screen); break;
        case PANTALLA_CONFIGURACION: create_config_screen(next_screen); break;
        case PANTALLA_WIFI_REDES: create_wifi_network_screen(next_screen); break;
        case PANTALLA_WIFI_PASSWORD: create_wifi_password_screen(next_screen); break;
        case PANTALLA_WIFI_PROGRESS: create_wifi_progress_screen(next_screen); break;
        case PANTALLA_WIFI_RESULT: create_wifi_result_screen(next_screen); break;
        case PANTALLA_WIFI_OLVIDAR: create_wifi_forget_screen(next_screen); break;
        case PANTALLA_STORAGE_LOCAL: create_storage_screen(next_screen); break;
        case PANTALLA_STORAGE_CONFIRMAR: create_storage_confirmation_screen(next_screen, false); break;
        case PANTALLA_STORAGE_CONFIRMAR_FINAL: create_storage_confirmation_screen(next_screen, true); break;
        case PANTALLA_TRANSFERIR: create_transfer_screen(next_screen); break;
        case PANTALLA_TRANSFER_TECLADO: create_transfer_keypad_screen(next_screen); break;
        case PANTALLA_TRANSFER_CONFIRMAR: create_transfer_confirm_screen(next_screen); break;
        case PANTALLA_TRANSFER_RESULTADO: create_transfer_result_screen(next_screen); break;
    }
    create_performance_overlay(next_screen);
    performance_screen_create_ms = millis() - screen_create_started_ms;
    if (PERFORMANCE_TEST_ENABLED) {
        performance_stats[static_cast<uint8_t>(pantalla)].create_ms = performance_screen_create_ms;
        Serial.printf("[Perf] Create %s %lums\n", performance_screen_name(pantalla),
                      static_cast<unsigned long>(performance_screen_create_ms));
    }
    char date_text[48];
    char time_text[16];
    obtener_texto_fecha_hora(date_text, sizeof(date_text), time_text, sizeof(time_text));
    if (clock_label) lv_label_set_text(clock_label, time_text);
    if (boot_defer_screen_load) {
        boot_deferred_screen = next_screen;
        transition_in_progress = false;
        return;
    }
    performance_transition_from = previous_screen;
    performance_transition_to = pantalla;
    performance_transition_started_ms = millis();
    lv_scr_load_anim(next_screen, transition_animation_for(previous_screen, pantalla), 220, 0, true);
    transition_release_timer = lv_timer_create(transition_release_cb, 260, nullptr);
}

void create_ui()
{
    if (!boot_defer_screen_load) {
        lv_obj_t *screen = lv_scr_act();
        lv_obj_set_style_bg_color(screen, lv_color_hex(0xF7FBFF), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    }
    mostrar_pantalla(PANTALLA_ESPERA);
    lv_timer_create(update_clock_timer, 1000, nullptr);
    lv_timer_create(update_nfc_ui_timer, 100, nullptr); lv_timer_create([](lv_timer_t *) { update_wifi_ui(); update_wifi_setup_ui(); }, 500, nullptr);
    if (!boot_defer_screen_load && session_timeout_lv_timer == nullptr) {
        session_timeout_lv_timer = lv_timer_create(session_timeout_timer, 1000, nullptr);
    }
    performance_timer = lv_timer_create(update_performance_overlay, 1000, nullptr);
}

void startup_progress_timer_cb(lv_timer_t *timer)
{
    if (!boot_setup_complete) return;

    const uint32_t now = millis();
    if (now - boot_last_detail_update_ms >= 250) {
        boot_last_detail_update_ms = now;
        update_boot_detail();
    }

    if (!boot_wifi_resolved) {
        const WiFiState wifi_state = wifi_manager.state();
        const uint32_t elapsed = now - boot_wifi_started_ms;
        const bool disconnected_without_attempt =
            wifi_state == WiFiState::DISCONNECTED && elapsed >= 800;
        const bool connection_timeout = elapsed >= 13000;
        if (wifi_state == WiFiState::CONNECTED || wifi_state == WiFiState::ERROR ||
            disconnected_without_attempt || connection_timeout) {
            const bool connected = wifi_state == WiFiState::CONNECTED;
            set_boot_progress(76, connected ? "Wi-Fi conectado" :
                                               "Wi-Fi no disponible; continuando");
            set_boot_progress(82, time_manager.isSynchronized() ?
                                  "Hora sincronizada" : "Hora: pendiente; continuando");
            set_boot_progress(85, sd_manager.state() == SdState::SD_NOT_PRESENT ?
                                  "MicroSD: No instalada" : "MicroSD no disponible");
            boot_wifi_resolved = true;
        }
    }

    if (!boot_wifi_resolved) return;

    if (!boot_ui_prepared) {
        set_boot_progress(90, "Cargando interfaz...");
        boot_defer_screen_load = true;
        create_ui();
        boot_defer_screen_load = false;
        boot_ui_prepared = true;
        set_boot_progress(100, "Sistema listo");
        boot_ready_since_ms = millis();
        return;
    }

    if (millis() - boot_ready_since_ms < 350 || boot_deferred_screen == nullptr) return;

    boot_splash_active = false;
    boot_splash_screen = nullptr;
    boot_progress_bar = nullptr;
    boot_percent_label = nullptr;
    boot_state_label = nullptr;
    boot_detail_label = nullptr;
    lv_obj_t *ready_screen = boot_deferred_screen;
    boot_deferred_screen = nullptr;
    lv_scr_load_anim(ready_screen, LV_SCR_LOAD_ANIM_FADE_ON, 220, 0, true);
    if (session_timeout_lv_timer == nullptr) {
        session_timeout_lv_timer = lv_timer_create(session_timeout_timer, 1000, nullptr);
    }
    performance_last_frame_count = esp_lv_adapter_get_frame_count();
    performance_last_sample_ms = millis();
    if (PERFORMANCE_TEST_ENABLED && performance_test_timer == nullptr) {
        performance_test_timer = lv_timer_create(performance_test_step_cb,
                                                 PERFORMANCE_SCREEN_HOLD_MS, nullptr);
    }
    if (boot_progress_timer == timer) boot_progress_timer = nullptr;
    lv_timer_del(timer);
}
} // namespace

void updateBalanceUI()
{
    if (balance_amount_label == nullptr) return;
    char amount_text[16];
    const AccountRecord *account = getAccountByStudentId(selected_student->student_id);
    snprintf(amount_text, sizeof(amount_text), "%ld", account ? static_cast<long>(account->aureos) : 0L);
    lv_label_set_text(balance_amount_label, amount_text);
}

void setStudentBalance(int32_t aureos)
{
    setAccountBalance(selected_student->student_id, aureos);
    updateBalanceUI();
}

int32_t getStudentBalance()
{
    const AccountRecord *account = getAccountByStudentId(selected_student->student_id);
    return account ? account->aureos : 0;
}

void setup()
{
    Serial.begin(115200);
    Serial.println("================================");
    Serial.println("BANCO ESCOLAR");
    Serial.println("Sistema iniciado");
    Serial.println("Serial: 115200 baudios");
    Serial.println("================================");
    set_boot_progress(5, "Consola serial iniciada");
    time_manager.begin();
    set_boot_progress(12, "Servicio de hora iniciado");

    board = std::make_shared<Board>(); if ((board == nullptr) || !board->init()) halt_on_error(); if (!board->begin()) halt_on_error();
    set_boot_progress(28, "Placa y periféricos inicializados");
    const esp_lv_adapter_config_t lvgl_config = ESP_LV_ADAPTER_DEFAULT_CONFIG(); if (esp_lv_adapter_init(&lvgl_config) != ESP_OK) halt_on_error();
    set_boot_progress(36, "Motor gráfico iniciado");
    const esp_lv_adapter_display_config_t display_config = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(board->getLCD(), SCREEN_WIDTH, SCREEN_HEIGHT, ESP_LV_ADAPTER_ROTATE_0);
    lv_display_t *display = esp_lv_adapter_register_display(&display_config); if (display == nullptr) halt_on_error();
    set_boot_progress(40, "Pantalla registrada");
    const esp_lv_adapter_touch_config_t touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, board->getTouch()); if (esp_lv_adapter_register_touch(&touch_config) == nullptr) halt_on_error();
    if (esp_lv_adapter_lock(-1) != ESP_OK) halt_on_error();
    create_boot_splash();
    esp_lv_adapter_unlock();
    set_boot_progress(44, "Pantalla táctil lista");
    if (esp_lv_adapter_start() != ESP_OK) halt_on_error();
    boot_adapter_started = true;
    if (esp_lv_adapter_lock(-1) != ESP_OK) halt_on_error();
    boot_progress_timer = lv_timer_create(startup_progress_timer_cb, 100, nullptr);
    esp_lv_adapter_unlock();
    if (boot_progress_timer == nullptr) halt_on_error();

    if (!PN532_ENABLED) {
        nfc_set_state(NFC_NO_DEVICE);
        Serial.println("[PN532] Deshabilitado temporalmente");
    }
    wifi_manager.begin();
    boot_wifi_started_ms = millis();
    set_boot_progress(50, "Servicio Wi-Fi iniciado");
    sd_manager.begin(); // No GPIO, mount, or filesystem access until the physical SD is available.
    set_boot_progress(56, "MicroSD: comprobando disponibilidad");
    const bool storage_ready = storage_manager.begin(); // Storage errors are non-fatal; continue to the UI.
    set_boot_progress(storage_ready ? 68 : 64,
                      storage_ready ? "Almacenamiento local listo" : "Almacenamiento local no disponible");
    boot_setup_complete = true;
    if (PN532_ENABLED) xTaskCreatePinnedToCore(nfc_task, "pn532_i2c1", 4096, nullptr, 1, &nfc_task_handle, 1);
}

void loop()
{
    wifi_manager.update();
    time_manager.update(wifi_manager.isConnected());
    storage_manager.updateSelfTest();
    vTaskDelay(pdMS_TO_TICKS(50));
}
