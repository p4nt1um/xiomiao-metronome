/*
 * UI：主界面（BPM 大字 + 拍点）、设置页（8 行滚动列表）、点击测速页。
 * 页面切换 = 删旧建新；根容器常驻，承接组焦点与按键分发。
 * ui_poll() 由主循环高频调用：提示音序列、测速超时、练习倒计时。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "esp_log.h"

#include "app_settings.h"
#include "bsp_buttons.h"
#include "bsp_buzzer.h"
#include "light_ctl.h"
#include "metro_engine.h"
#include "ui.h"

LV_FONT_DECLARE(ui_font_cn12);
LV_FONT_DECLARE(ui_font_cn16);

#define COL_BG          0x0E1116
#define COL_TEXT        0xE8EAED
#define COL_MUTED       0x7D8794
#define COL_ACCENT      0xFFC53D
#define COL_WEAK_DOT    0x39424D
#define COL_LINE        0x232A33
#define COL_SEL_BG      0x1E2833
#define COL_PAUSE       0x4FC3F7  /* 已暂停：青色 */

/* 拍点区域：BPM 右侧 x80..158，共 78px */
#define DOT_AREA_X      80
#define DOT_AREA_W      78
#define DOT_MAX         9

#define TAP_DOT_MAX     8
#define TAP_TIMEOUT_MS  2000
#define TAP_MIN_DT_MS   120

typedef enum {
    UI_PAGE_MAIN = 0,
    UI_PAGE_SETTINGS,
    UI_PAGE_TAP,
} ui_page_t;

typedef enum {
    SET_ROW_TIMESIG = 0,
    SET_ROW_SUBDIV,
    SET_ROW_ACCENT,
    SET_ROW_VOLUME,
    SET_ROW_MUTE,
    SET_ROW_TIMER,
    SET_ROW_LCTRL,
    SET_ROW_TAP,
    SET_ROW_RESET,
    SET_ROW_COUNT,
} set_row_id_t;

static const char *s_set_labels[SET_ROW_COUNT] = {
    "拍号", "细分", "重音", "音量", "静音小节", "练习计时", "光控暂停", "点击测速", "恢复默认",
};
static const char *s_subdiv_names[METRO_SUBDIV_MAX] = {"四分", "八分", "三连音", "十六分"};
static const int s_timer_choices[APP_TIMER_CHOICE_COUNT] = APP_TIMER_CHOICES;

static struct {
    lv_obj_t *root;
    ui_page_t page;
    lv_obj_t *page_obj;
    /* 主界面 */
    lv_obj_t *timesig;
    lv_obj_t *term;
    lv_obj_t *status;
    lv_obj_t *bpm;
    lv_obj_t *bpm_sub;
    lv_obj_t *dots[DOT_MAX];
    /* 设置页 */
    lv_obj_t *rows[SET_ROW_COUNT];
    lv_obj_t *row_labels[SET_ROW_COUNT];
    lv_obj_t *row_vals[SET_ROW_COUNT];
    lv_obj_t *scroll;
    int sel;
    int mute_field;          /* 0=播 1=静 */
    /* 测速页 */
    lv_obj_t *tap_bpm;
    lv_obj_t *tap_dots[TAP_DOT_MAX];
} s_ui;

static metro_cfg_t s_cfg;

/* 练习计时与状态（暂停时剩余时间冻结） */
static bool s_practice_on;
static uint32_t s_practice_remain_ms;
static uint32_t s_practice_last_ms;
static bool s_muted_bar;
static char s_status_cache[16];

/* 提示音序列（ui_poll 驱动） */
static struct {
    uint8_t left;
    uint32_t freq;
    uint32_t dur_ms;
    uint32_t next_at;
} s_beeps;

/* 点击测速 */
static uint32_t s_tap_last_ms;
static uint32_t s_tap_iv[TAP_DOT_MAX];
static uint8_t s_tap_iv_count;
static uint32_t s_tap_iv_sum;
static uint16_t s_tap_total;

/* 恢复默认长按状态见 reset_hold_poll() */

static void ui_show_page(ui_page_t page);

/* ---------------- 工具 ---------------- */

static const char *bpm_term(int bpm)
{
    if (bpm <= 20) {
        return "Larghissimo";
    }
    if (bpm <= 40) {
        return "Grave";
    }
    if (bpm <= 45) {
        return "Lento";
    }
    if (bpm <= 50) {
        return "Largo";
    }
    if (bpm <= 60) {
        return "Adagio";
    }
    if (bpm <= 70) {
        return "Adagietto";
    }
    if (bpm <= 85) {
        return "Andante";
    }
    if (bpm <= 97) {
        return "Moderato";
    }
    if (bpm <= 109) {
        return "Allegretto";
    }
    if (bpm <= 132) {
        return "Allegro";
    }
    if (bpm <= 140) {
        return "Vivace";
    }
    if (bpm <= 177) {
        return "Presto";
    }
    return "Prestissimo";
}

static const char *timesig_str(int beats)
{
    static const char *tbl[] = {"1", "2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "8/8", "9/8"};
    if (beats < 1 || beats > 9) {
        return "?";
    }
    return tbl[beats - 1];
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *make_line(lv_obj_t *parent, int y)
{
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_pos(line, 0, y);
    lv_obj_set_size(line, 160, 2);
    lv_obj_set_style_bg_color(line, lv_color_hex(COL_LINE), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    return line;
}

static void request_beeps(uint32_t freq_hz, uint8_t count, uint32_t dur_ms, uint32_t gap_ms)
{
    s_beeps.freq = freq_hz;
    s_beeps.dur_ms = dur_ms;
    s_beeps.left = count;
    s_beeps.next_at = lv_tick_get() + gap_ms;
}

static uint8_t cur_volume_duty(void)
{
    return (uint8_t)(s_cfg.volume * 255 / METRO_VOL_MAX);
}

static void beeps_poll(void)
{
    if (s_beeps.left == 0) {
        return;
    }
    const uint32_t now = lv_tick_get();
    if ((int32_t)(now - s_beeps.next_at) >= 0) {
        bsp_buzzer_tone(s_beeps.freq, cur_volume_duty(), s_beeps.dur_ms);
        s_beeps.left--;
        s_beeps.next_at = now + s_beeps.dur_ms + 90;
    }
}

/* ---------------- 拍点 ---------------- */

static void dots_apply(int current)
{
    if (s_ui.page != UI_PAGE_MAIN || s_ui.dots[0] == NULL) {
        return;
    }
    const int n = s_cfg.beats_per_bar;
    int size, pitch, per_row;
    if (n <= 4) {
        size = 13;
        pitch = 19;
        per_row = n;
    }
    else if (n <= 6) {
        size = 11;
        pitch = 13;
        per_row = n;
    }
    else {
        size = 10;
        pitch = 12;
        per_row = 5;
    }
    const int rows = (n + per_row - 1) / per_row;
    const int row_gap = 29;
    const int cy0 = 60 - (rows - 1) * row_gap / 2;

    for (int i = 0; i < DOT_MAX; ++i) {
        lv_obj_t *dot = s_ui.dots[i];
        if (i >= n) {
            lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
        const int r = i / per_row;
        const int cnt = (n - r * per_row < per_row) ? (n - r * per_row) : per_row;
        const int total_w = (cnt - 1) * pitch + size;
        const int cx = DOT_AREA_X + (DOT_AREA_W - total_w) / 2 + (i % per_row) * pitch + size / 2;
        const int cy = cy0 + r * row_gap;
        const bool active = (i == current);
        const int sz = active ? size + 4 : size;
        lv_obj_set_size(dot, sz, sz);
        lv_obj_set_pos(dot, cx - sz / 2, cy - sz / 2);
        uint32_t color;
        if (s_muted_bar) { /* 静音训练静音段：拍点变暗但仍走动 */
            color = active ? COL_MUTED : COL_WEAK_DOT;
        }
        else {
            color = (i == 0) ? COL_ACCENT : COL_WEAK_DOT;
            if (active) {
                color = COL_TEXT;
            }
        }
        lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    }
}

/* ---------------- 主界面 ---------------- */

static void main_set_status(const char *text, uint32_t color)
{
    if (strcmp(s_status_cache, text) == 0) {
        return;
    }
    strcpy(s_status_cache, text);
    lv_label_set_text(s_ui.status, text);
    lv_obj_set_style_text_color(s_ui.status, lv_color_hex(color), 0);
}

static void fmt_countdown(char *buf, size_t len)
{
    int remain = (int)(s_practice_remain_ms / 1000);
    if (remain < 0) {
        remain = 0;
    }
    snprintf(buf, len, "%02d:%02d", (remain / 60) % 100, remain % 60);
}

static void main_refresh_status(void)
{
    if (s_ui.page != UI_PAGE_MAIN || s_ui.status == NULL) {
        return;
    }
    const metro_state_t st = metro_engine_state();
    if (st == METRO_STATE_PAUSED) {
        const int timer_min = app_settings_get_timer_min();
        if (timer_min > 0 && s_practice_on &&
            (lv_tick_get() / 1000) % 2 != 0) { /* 暂停中：与剩余时间每秒交替 */
            char buf[10];
            fmt_countdown(buf, sizeof(buf));
            main_set_status(buf, COL_PAUSE);
        }
        else {
            main_set_status("已暂停", COL_PAUSE);
        }
        return;
    }
    if (st == METRO_STATE_STOPPED) {
        main_set_status("已停止", COL_MUTED);
        return;
    }
    if (s_muted_bar) {
        main_set_status("静音中", COL_WEAK_DOT);
        return;
    }
    const int timer_min = app_settings_get_timer_min();
    if (timer_min > 0 && s_practice_on) {
        char buf[10];
        fmt_countdown(buf, sizeof(buf));
        main_set_status(buf, COL_ACCENT);
        return;
    }
    main_set_status("运行中", COL_ACCENT);
}

static void main_update_bpm_widgets(void)
{
    lv_label_set_text_fmt(s_ui.bpm, "%d", s_cfg.bpm);
    lv_label_set_text_fmt(s_ui.bpm_sub, "♩=%d", s_cfg.bpm);
    lv_label_set_text(s_ui.term, bpm_term(s_cfg.bpm));
    lv_label_set_text(s_ui.timesig, timesig_str(s_cfg.beats_per_bar));
}

static void build_main_page(lv_obj_t *parent)
{
    s_ui.timesig = make_label(parent, &ui_font_cn16, COL_TEXT);
    lv_obj_set_pos(s_ui.timesig, 4, 2);

    s_ui.term = make_label(parent, &ui_font_cn12, COL_MUTED);
    lv_obj_set_pos(s_ui.term, 46, 6);

    s_ui.status = make_label(parent, &ui_font_cn12, COL_MUTED);
    lv_obj_set_pos(s_ui.status, 96, 6);
    lv_obj_set_width(s_ui.status, 60);
    lv_obj_set_style_text_align(s_ui.status, LV_TEXT_ALIGN_RIGHT, 0);
    s_status_cache[0] = '\0';

    make_line(parent, 22);

    s_ui.bpm = make_label(parent, &lv_font_montserrat_40, COL_TEXT);
    lv_obj_set_pos(s_ui.bpm, 4, 38);

    s_ui.bpm_sub = make_label(parent, &ui_font_cn12, COL_MUTED);
    lv_obj_set_pos(s_ui.bpm_sub, 8, 84);

    for (int i = 0; i < DOT_MAX; ++i) {
        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_remove_style_all(dot);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(COL_WEAK_DOT), 0);
        s_ui.dots[i] = dot;
    }

    make_line(parent, 104);

    lv_obj_t *hint = make_label(parent, &ui_font_cn12, COL_MUTED);
    lv_obj_set_pos(hint, 0, 110);
    lv_obj_set_width(hint, 160);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint, "▲▼◀▶调速  A启停  B菜单");

    main_update_bpm_widgets();
    dots_apply(-1);
    main_refresh_status();
}

/* ---------------- 设置页 ---------------- */

static void set_value_text(char *buf, size_t len, int row)
{
    switch (row) {
    case SET_ROW_TIMESIG:
        snprintf(buf, len, "◀ %s ▶", timesig_str(s_cfg.beats_per_bar));
        break;
    case SET_ROW_SUBDIV:
        snprintf(buf, len, "◀ %s ▶", s_subdiv_names[s_cfg.subdiv - 1]);
        break;
    case SET_ROW_ACCENT:
        snprintf(buf, len, "◀ %s ▶", s_cfg.accent ? "开" : "关");
        break;
    case SET_ROW_VOLUME:
        snprintf(buf, len, "◀ %d/%d ▶", s_cfg.volume, METRO_VOL_MAX);
        break;
    case SET_ROW_MUTE:
        /* 双字段，当前可调字段高亮（label 开了 recolor：#RRGGBB 文字#，参数后必须有空格） */
        if (s_ui.mute_field == 0) {
            snprintf(buf, len, "#%06X 播%d# #%06X 静%d#",
                     COL_ACCENT, s_cfg.mute_play, COL_MUTED, s_cfg.mute_rest);
        }
        else {
            snprintf(buf, len, "#%06X 播%d# #%06X 静%d#",
                     COL_MUTED, s_cfg.mute_play, COL_ACCENT, s_cfg.mute_rest);
        }
        break;
    case SET_ROW_TIMER: {
        const int t = app_settings_get_timer_min();
        if (t > 0) {
            snprintf(buf, len, "◀ %d分钟 ▶", t);
        }
        else {
            snprintf(buf, len, "◀ 关 ▶");
        }
        break;
    }
    case SET_ROW_LCTRL:
        snprintf(buf, len, "◀ %s ▶", app_settings_get_light_ctrl() ? "开" : "关");
        break;
    case SET_ROW_TAP:
        snprintf(buf, len, "A:进入");
        break;
    default:
        snprintf(buf, len, "A:长按");
        break;
    }
}

static void settings_refresh_rows(void)
{
    char buf[40];

    for (int i = 0; i < SET_ROW_COUNT; ++i) {
        const bool sel = (i == s_ui.sel);
        lv_obj_set_style_bg_color(s_ui.rows[i], lv_color_hex(COL_SEL_BG), 0);
        lv_obj_set_style_bg_opa(s_ui.rows[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(s_ui.row_labels[i],
                                    lv_color_hex(sel ? COL_ACCENT : COL_TEXT), 0);
        lv_obj_set_style_text_color(s_ui.row_vals[i],
                                    lv_color_hex(sel ? COL_ACCENT : COL_TEXT), 0);
        lv_label_set_text(s_ui.row_labels[i], s_set_labels[i]);
        set_value_text(buf, sizeof(buf), i);
        lv_label_set_text(s_ui.row_vals[i], buf);
    }
}

static void build_settings_page(lv_obj_t *parent)
{
    lv_obj_t *title = make_label(parent, &ui_font_cn16, COL_TEXT);
    lv_obj_set_pos(title, 6, 3);
    lv_label_set_text(title, "设置");

    lv_obj_t *back = make_label(parent, &ui_font_cn12, COL_MUTED);
    lv_obj_set_pos(back, 100, 6);
    lv_obj_set_width(back, 54);
    lv_obj_set_style_text_align(back, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(back, "B:返回");

    make_line(parent, 20);

    lv_obj_t *scroll = lv_obj_create(parent);
    lv_obj_remove_style_all(scroll);
    lv_obj_set_pos(scroll, 0, 22);
    lv_obj_set_size(scroll, 160, 84);
    lv_obj_set_style_pad_all(scroll, 0, 0);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scroll, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(scroll, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
    s_ui.scroll = scroll;

    for (int i = 0; i < SET_ROW_COUNT; ++i) {
        lv_obj_t *row = lv_obj_create(scroll);
        lv_obj_remove_style_all(row);
        lv_obj_set_pos(row, 3, i * 27);
        lv_obj_set_size(row, 154, 26);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        s_ui.rows[i] = row;

        s_ui.row_labels[i] = make_label(row, &ui_font_cn16, COL_TEXT);
        lv_obj_set_pos(s_ui.row_labels[i], 8, 4);

        s_ui.row_vals[i] = make_label(row, &ui_font_cn12, COL_TEXT);
        lv_obj_set_pos(s_ui.row_vals[i], 46, 6);
        lv_obj_set_width(s_ui.row_vals[i], 102);
        lv_obj_set_style_text_align(s_ui.row_vals[i], LV_TEXT_ALIGN_RIGHT, 0);
        if (i == SET_ROW_MUTE) {
            lv_label_set_recolor(s_ui.row_vals[i], true);
        }
    }

    make_line(parent, 106);

    lv_obj_t *footer = make_label(parent, &ui_font_cn12, COL_MUTED);
    lv_obj_set_pos(footer, 0, 111);
    lv_obj_set_width(footer, 160);
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(footer, "▲▼ ◀▶  A确认 B返回");

    settings_refresh_rows();
    lv_obj_scroll_to_view(s_ui.rows[s_ui.sel], LV_ANIM_OFF);
}

/* ---------------- 点击测速页 ---------------- */

static int tap_current_bpm(void)
{
    if (s_tap_iv_count == 0 || s_tap_iv_sum == 0) {
        return 0;
    }
    const uint32_t avg = s_tap_iv_sum / s_tap_iv_count;
    if (avg == 0) {
        return 0;
    }
    int bpm = (int)(60000UL / avg);
    if (bpm < METRO_BPM_MIN) {
        bpm = METRO_BPM_MIN;
    }
    if (bpm > METRO_BPM_MAX) {
        bpm = METRO_BPM_MAX;
    }
    return bpm;
}

static void tap_refresh(void)
{
    const int bpm = tap_current_bpm();
    if (bpm > 0 && s_tap_iv_count >= 1) {
        lv_label_set_text_fmt(s_ui.tap_bpm, "%d", bpm);
    }
    else {
        lv_label_set_text(s_ui.tap_bpm, "--");
    }
    const int shown = (s_tap_total < TAP_DOT_MAX) ? s_tap_total : TAP_DOT_MAX;
    for (int i = 0; i < TAP_DOT_MAX; ++i) {
        lv_obj_set_style_bg_color(s_ui.tap_dots[i],
                                  lv_color_hex(i < shown ? COL_ACCENT : COL_WEAK_DOT), 0);
    }
}

static void tap_reset(void)
{
    s_tap_last_ms = 0;
    s_tap_iv_count = 0;
    s_tap_iv_sum = 0;
    s_tap_total = 0;
}

static void build_tap_page(lv_obj_t *parent)
{
    lv_obj_t *title = make_label(parent, &ui_font_cn16, COL_TEXT);
    lv_obj_set_pos(title, 6, 3);
    lv_label_set_text(title, "点击测速");

    lv_obj_t *back = make_label(parent, &ui_font_cn12, COL_MUTED);
    lv_obj_set_pos(back, 100, 6);
    lv_obj_set_width(back, 54);
    lv_obj_set_style_text_align(back, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(back, "B:返回");

    make_line(parent, 20);

    s_ui.tap_bpm = make_label(parent, &lv_font_montserrat_40, COL_TEXT);
    lv_obj_set_pos(s_ui.tap_bpm, 0, 32);
    lv_obj_set_width(s_ui.tap_bpm, 160);
    lv_obj_set_style_text_align(s_ui.tap_bpm, LV_TEXT_ALIGN_CENTER, 0);

    for (int i = 0; i < TAP_DOT_MAX; ++i) {
        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_remove_style_all(dot);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(COL_WEAK_DOT), 0);
        lv_obj_set_size(dot, 9, 9);
        lv_obj_set_pos(dot, 30 + i * 13, 84);
        s_ui.tap_dots[i] = dot;
    }

    make_line(parent, 104);

    lv_obj_t *footer = make_label(parent, &ui_font_cn12, COL_MUTED);
    lv_obj_set_pos(footer, 0, 110);
    lv_obj_set_width(footer, 160);
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(footer, "A:敲击  2秒超时自动返回");

    tap_refresh();
}

static void tap_apply_and_exit(void)
{
    const int bpm = tap_current_bpm();
    if (bpm > 0) {
        s_cfg.bpm = bpm;
        metro_engine_set_cfg(&s_cfg);
        app_settings_set(&s_cfg);
    }
    tap_reset();
    ui_show_page(UI_PAGE_SETTINGS);
}

static void tap_on_press(void)
{
    const uint32_t now = lv_tick_get();

    if (s_tap_last_ms != 0) {
        const uint32_t dt = now - s_tap_last_ms;
        if (dt > TAP_TIMEOUT_MS || dt < TAP_MIN_DT_MS) {
            tap_reset(); /* 异常间隔：丢弃重新计 */
        }
        else {
            if (s_tap_iv_count == TAP_DOT_MAX) {
                s_tap_iv_sum -= s_tap_iv[0];
                memmove(s_tap_iv, s_tap_iv + 1, sizeof(s_tap_iv) - sizeof(s_tap_iv[0]));
                s_tap_iv_count--;
            }
            s_tap_iv[s_tap_iv_count] = dt;
            s_tap_iv_sum += dt;
            s_tap_iv_count++;
        }
    }
    s_tap_last_ms = now;
    s_tap_total++;
    tap_refresh();
}

/* ---------------- 页面切换 ---------------- */

static void ui_show_page(ui_page_t page)
{
    if (s_ui.page_obj) {
        lv_obj_delete(s_ui.page_obj);
        memset(s_ui.dots, 0, sizeof(s_ui.dots));
        memset(s_ui.tap_dots, 0, sizeof(s_ui.tap_dots));
        s_ui.status = NULL;
        s_ui.tap_bpm = NULL;
        s_ui.scroll = NULL;
    }
    s_ui.page = page;

    s_ui.page_obj = lv_obj_create(s_ui.root);
    lv_obj_remove_style_all(s_ui.page_obj);
    lv_obj_set_pos(s_ui.page_obj, 0, 0);
    lv_obj_set_size(s_ui.page_obj, 160, 128);
    lv_obj_clear_flag(s_ui.page_obj, LV_OBJ_FLAG_SCROLLABLE);

    if (page == UI_PAGE_MAIN) {
        build_main_page(s_ui.page_obj);
    }
    else if (page == UI_PAGE_SETTINGS) {
        build_settings_page(s_ui.page_obj);
    }
    else {
        build_tap_page(s_ui.page_obj);
    }
}

/* ---------------- 恢复默认 ---------------- */

static void factory_reset(void)
{
    s_cfg = (metro_cfg_t){
        .bpm = 120,
        .beats_per_bar = 4,
        .subdiv = 1,
        .accent = true,
        .volume = 6,
        .mute_play = 3,
        .mute_rest = 0,
    };
    app_settings_set_timer_min(0);
    metro_engine_set_cfg(&s_cfg);
    app_settings_set(&s_cfg);
    request_beeps(1046, 2, 80, 60);
    ESP_LOGI("ui", "factory reset done");
}

/* ---------------- 按键 ---------------- */

static void apply_cfg_change(void)
{
    metro_engine_set_cfg(&s_cfg);
    app_settings_set(&s_cfg);
}

static void main_adjust_bpm(int delta)
{
    int nb = s_cfg.bpm + delta;
    if (nb < METRO_BPM_MIN) {
        nb = METRO_BPM_MIN;
    }
    if (nb > METRO_BPM_MAX) {
        nb = METRO_BPM_MAX;
    }
    if (nb == s_cfg.bpm) {
        return;
    }
    s_cfg.bpm = nb;
    apply_cfg_change();
    main_update_bpm_widgets();
}

static void main_toggle_run(void)
{
    const metro_state_t st = metro_engine_state();

    if (st == METRO_STATE_STOPPED) {
        s_muted_bar = false;
        metro_engine_start();
        const int timer_min = app_settings_get_timer_min();
        if (timer_min > 0) {
            s_practice_on = true;
            s_practice_remain_ms = (uint32_t)timer_min * 60U * 1000U;
            s_practice_last_ms = lv_tick_get();
        }
    }
    else if (st == METRO_STATE_RUNNING) {
        metro_engine_stop();
        s_practice_on = false;
        s_muted_bar = false;
        dots_apply(-1);
    }
    else { /* PAUSED：A 键 = 恢复 */
        s_muted_bar = false;
        metro_engine_resume();
        if (s_practice_on) {
            s_practice_last_ms = lv_tick_get();
        }
    }
    main_refresh_status();
}

static void settings_adjust(int dir)
{
    switch (s_ui.sel) {
    case SET_ROW_TIMESIG:
        s_cfg.beats_per_bar += dir;
        if (s_cfg.beats_per_bar < METRO_BEATS_MIN) {
            s_cfg.beats_per_bar = METRO_BEATS_MAX;
        }
        if (s_cfg.beats_per_bar > METRO_BEATS_MAX) {
            s_cfg.beats_per_bar = METRO_BEATS_MIN;
        }
        break;
    case SET_ROW_SUBDIV:
        s_cfg.subdiv += dir;
        if (s_cfg.subdiv < METRO_SUBDIV_MIN) {
            s_cfg.subdiv = METRO_SUBDIV_MAX;
        }
        if (s_cfg.subdiv > METRO_SUBDIV_MAX) {
            s_cfg.subdiv = METRO_SUBDIV_MIN;
        }
        break;
    case SET_ROW_ACCENT:
        s_cfg.accent = (dir > 0);
        break;
    case SET_ROW_VOLUME:
        s_cfg.volume += dir;
        if (s_cfg.volume < METRO_VOL_MIN) {
            s_cfg.volume = METRO_VOL_MIN;
        }
        if (s_cfg.volume > METRO_VOL_MAX) {
            s_cfg.volume = METRO_VOL_MAX;
        }
        break;
    case SET_ROW_MUTE:
        if (s_ui.mute_field == 0) {
            s_cfg.mute_play += dir;
            if (s_cfg.mute_play < METRO_MUTE_PLAY_MIN) {
                s_cfg.mute_play = METRO_MUTE_PLAY_MIN;
            }
            if (s_cfg.mute_play > METRO_MUTE_PLAY_MAX) {
                s_cfg.mute_play = METRO_MUTE_PLAY_MAX;
            }
        }
        else {
            s_cfg.mute_rest += dir;
            if (s_cfg.mute_rest < METRO_MUTE_REST_MIN) {
                s_cfg.mute_rest = METRO_MUTE_REST_MIN;
            }
            if (s_cfg.mute_rest > METRO_MUTE_REST_MAX) {
                s_cfg.mute_rest = METRO_MUTE_REST_MAX;
            }
        }
        break;
    case SET_ROW_TIMER: {
        int idx = 0;
        const int cur = app_settings_get_timer_min();
        for (int i = 0; i < APP_TIMER_CHOICE_COUNT; ++i) {
            if (s_timer_choices[i] == cur) {
                idx = i;
                break;
            }
        }
        idx = (idx + dir + APP_TIMER_CHOICE_COUNT) % APP_TIMER_CHOICE_COUNT;
        app_settings_set_timer_min(s_timer_choices[idx]);
        break;
    }
    case SET_ROW_LCTRL:
        app_settings_set_light_ctrl(dir > 0);
        break;
    default:
        return;
    }

    if (s_ui.sel != SET_ROW_TIMER && s_ui.sel != SET_ROW_LCTRL) {
        apply_cfg_change();
    }
    settings_refresh_rows();

    if (s_ui.sel == SET_ROW_VOLUME && s_cfg.volume > 0) { /* 音量即时试听 */
        bsp_buzzer_tone(880, cur_volume_duty(), 60);
    }
}

static void settings_select(int delta)
{
    s_ui.sel = (s_ui.sel + delta + SET_ROW_COUNT) % SET_ROW_COUNT;
    settings_refresh_rows();
    lv_obj_scroll_to_view(s_ui.rows[s_ui.sel], LV_ANIM_OFF);
}

static void settings_on_enter(void)
{
    switch (s_ui.sel) {
    case SET_ROW_MUTE:
        s_ui.mute_field ^= 1;
        settings_refresh_rows();
        break;
    case SET_ROW_TAP:
        tap_reset();
        ui_show_page(UI_PAGE_TAP);
        break;
    default:
        break;
    }
}

static void key_event_cb(lv_event_t *e)
{
    const uint32_t key = lv_event_get_key(e);

    if (s_ui.page == UI_PAGE_MAIN) {
        switch (key) {
        case LV_KEY_UP:
            main_adjust_bpm(+1);
            break;
        case LV_KEY_DOWN:
            main_adjust_bpm(-1);
            break;
        case LV_KEY_LEFT:
            main_adjust_bpm(-10);
            break;
        case LV_KEY_RIGHT:
            main_adjust_bpm(+10);
            break;
        case LV_KEY_ENTER:
            main_toggle_run();
            break;
        case LV_KEY_ESC:
            s_ui.sel = 0;
            s_ui.mute_field = 0;
            ui_show_page(UI_PAGE_SETTINGS);
            break;
        default:
            break;
        }
    }
    else if (s_ui.page == UI_PAGE_SETTINGS) {
        switch (key) {
        case LV_KEY_UP:
            settings_select(-1);
            break;
        case LV_KEY_DOWN:
            settings_select(+1);
            break;
        case LV_KEY_LEFT:
            settings_adjust(-1);
            break;
        case LV_KEY_RIGHT:
            settings_adjust(+1);
            break;
        case LV_KEY_ENTER:
            settings_on_enter();
            break;
        case LV_KEY_ESC:
            ui_show_page(UI_PAGE_MAIN);
            break;
        default:
            break;
        }
    }
    else { /* UI_PAGE_TAP */
        switch (key) {
        case LV_KEY_ENTER:
            tap_on_press();
            break;
        case LV_KEY_ESC:
            tap_apply_and_exit();
            break;
        default:
            break;
        }
    }
}

/* A 键长按 1 秒确认恢复默认：直接轮询 GPIO（不依赖 LVGL 的按压事件路径） */
static void reset_hold_poll(void)
{
    static bool raw_last;
    static bool stable;
    static uint32_t changed_ms;
    static bool holding;
    static uint32_t hold_start_ms;
    static bool fired;

    const bool raw = bsp_buttons_is_pressed(BSP_BTN_A);
    const uint32_t now = lv_tick_get();

    if (raw != raw_last) {
        raw_last = raw;
        changed_ms = now;
    }
    if (lv_tick_elaps(changed_ms) >= 25 && raw != stable) {
        stable = raw;
    }

    if (stable) {
        if (!holding) {
            holding = true;
            hold_start_ms = now;
            fired = false;
        }
        if (!fired && s_ui.page == UI_PAGE_SETTINGS && s_ui.sel == SET_ROW_RESET &&
            lv_tick_elaps(hold_start_ms) >= 1000) {
            fired = true;
            factory_reset();
            settings_refresh_rows();
        }
    }
    else {
        holding = false;
    }
}

/* ---------------- 对外接口 ---------------- */

void ui_init(lv_group_t *group)
{
    app_settings_get(&s_cfg);

    s_ui.root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_ui.root);
    lv_obj_set_pos(s_ui.root, 0, 0);
    lv_obj_set_size(s_ui.root, 160, 128);
    lv_obj_set_style_bg_color(s_ui.root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_ui.root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ui.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_group_add_obj(group, s_ui.root);
    lv_obj_add_event_cb(s_ui.root, key_event_cb, LV_EVENT_KEY, NULL);

    ui_show_page(UI_PAGE_MAIN);
}

void ui_on_beat(const metro_beat_event_t *ev)
{
    if (s_ui.page != UI_PAGE_MAIN) {
        return;
    }
    s_muted_bar = ev->muted;
    dots_apply(ev->beat);
    main_refresh_status();
}

void ui_poll(void)
{
    beeps_poll();
    reset_hold_poll();

    if (s_ui.page == UI_PAGE_TAP && s_tap_last_ms != 0 &&
        lv_tick_elaps(s_tap_last_ms) > TAP_TIMEOUT_MS) {
        tap_apply_and_exit();
        return;
    }

    /* 光控暂停手势：仅主界面且非停止态 */
    const bool lctrl_enabled = app_settings_get_light_ctrl() > 0;
    const bool lctrl_active = lctrl_enabled && s_ui.page == UI_PAGE_MAIN &&
                              metro_engine_state() != METRO_STATE_STOPPED;
    if (light_ctl_poll(lctrl_active) == LIGHT_CTL_TOGGLE) {
        if (metro_engine_state() == METRO_STATE_RUNNING) {
            metro_engine_pause();
            bsp_buzzer_tone(660, cur_volume_duty(), 80); /* 暂停低音反馈 */
            ESP_LOGI("ui", "light gesture -> pause");
        }
        else { /* PAUSED：恢复自带第 1 拍强拍 */
            metro_engine_resume();
            if (s_practice_on) {
                s_practice_last_ms = lv_tick_get();
            }
            ESP_LOGI("ui", "light gesture -> resume");
        }
        main_refresh_status();
    }

    /* 练习计时：仅运行态倒计时，暂停时冻结 */
    if (s_practice_on && metro_engine_state() == METRO_STATE_RUNNING) {
        const uint32_t now = lv_tick_get();
        const uint32_t dt = lv_tick_elaps(s_practice_last_ms);
        s_practice_last_ms = now;
        s_practice_remain_ms = (s_practice_remain_ms > dt) ? (s_practice_remain_ms - dt) : 0;
        if (s_practice_remain_ms == 0) {
            metro_engine_stop();
            s_practice_on = false;
            s_muted_bar = false;
            if (s_ui.page == UI_PAGE_MAIN) {
                dots_apply(-1);
            }
            main_refresh_status();
            request_beeps(1568, 3, 120, 100); /* 到时提示音 */
        }
    }

    main_refresh_status(); /* 倒计时每秒刷新 */
}
