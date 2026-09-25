#include "PowerSupplyDisplayUi.h"

#include "PowerSupplyDisplayFonts.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 *  布局常量（单位 px，屏幕 240x240）
 *
 *    y=0   .. 118  上半屏：总线电源
 *    y=119 .. 120  分隔线
 *    y=121 .. 239  下半屏：辅助电源
 *
 *  每半屏：标题栏 22 + 5 行 x 19 = 117（<= 119）
 * ========================================================================== */
#define UI_SCREEN_W 240
#define UI_SCREEN_H 240
#define UI_HALF_H 119
#define UI_HALF2_Y 121
#define UI_HEADER_H 22
#define UI_ROW_H 19
#define UI_ROW_FIRST_Y 24

#define UI_ROW_COUNT 5

#define UI_COL_LABEL_X 8
#define UI_COL_VALUE_X 84
#define UI_COL_VALUE_W 148

#define UI_FONT_SMALL (&lv_font_ps_16)
#define UI_FONT_TITLE (&lv_font_ps_20)

static const char *const cRowNames[UI_ROW_COUNT] = {"输入电压", "输入电流", "输出电压", "输出电流", "温度"};
static const char *const cSideNames[2] = {"总线电源", "辅助电源"};
static const char *const cStatusNames[] = {"无数据", "正常", "已关闭", "故障"};

// 状态索引
enum UiStatus : uint8_t
{
    UiStatus_NoData = 0,
    UiStatus_Ok,
    UiStatus_Off,
    UiStatus_Fault,
};

// 颜色
#define UI_COL_BG lv_color_hex(0x000000)
#define UI_COL_HEADER_BG lv_color_hex(0x14213D)
#define UI_COL_SEPARATOR lv_color_hex(0x303030)
#define UI_COL_LABEL lv_color_hex(0x9FB6CE)
#define UI_COL_VALUE lv_color_hex(0xFFFFFF)
#define UI_COL_OK lv_color_hex(0x39D353)
#define UI_COL_OFF lv_color_hex(0x8A8A8A)
#define UI_COL_FAULT lv_color_hex(0xFF4D4D)
#define UI_COL_DIALOG_BG lv_color_hex(0x2A0000)
#define UI_COL_DIALOG_BORDER lv_color_hex(0xFF3B30)
#define UI_COL_DIALOG_TITLE lv_color_hex(0xFF6B6B)
#define UI_COL_DIALOG_HINT lv_color_hex(0xFFB0B0)

static lv_obj_t *sValueLabel[2][UI_ROW_COUNT] = {{nullptr}};
static lv_obj_t *sStatusLabel[2] = {nullptr};
static char sValueCache[2][UI_ROW_COUNT][24] = {{{0}}};
static char sStatusCache[2][16] = {{0}};

static lv_obj_t *sAlarmRoot = nullptr;
static lv_obj_t *sAlarmTitle = nullptr;
static lv_obj_t *sAlarmBody = nullptr;
static char sAlarmBodyCache[256] = {0};

/* ============================================================================
 *  数值格式化
 * ========================================================================== */

/// 定点输出，例如 scaled=28950, decimals=1, unit="V" -> "28.9 V"
static void formatFixed(char *out, size_t outSize, int32_t scaled, uint8_t decimals, const char *unit)
{
    const bool negative = scaled < 0;
    const uint32_t v = (uint32_t)(negative ? -scaled : scaled);
    const char *sign = negative ? "-" : "";

    if (decimals == 2)
        snprintf(out, outSize, "%s%u.%02u %s", sign, (unsigned)(v / 100u), (unsigned)(v % 100u), unit);
    else if (decimals == 1)
        snprintf(out, outSize, "%s%u.%u %s", sign, (unsigned)(v / 10u), (unsigned)(v % 10u), unit);
    else
        snprintf(out, outSize, "%s%u %s", sign, (unsigned)v, unit);
}

/// 电压统一用 V + 1 位小数（输入 18..32 V、输出 24..30 V）
static void formatVoltage(char *out, size_t outSize, float mV)
{
    formatFixed(out, outSize, (int32_t)lroundf(mV / 100.0f), 1, "V");
}

/// 电流统一用 A + 2 位小数（10 mA 分辨率）
static void formatCurrent(char *out, size_t outSize, float mA)
{
    formatFixed(out, outSize, (int32_t)lroundf(mA / 10.0f), 2, "A");
}

static void formatTemperature(char *out, size_t outSize, float celsius)
{
    formatFixed(out, outSize, (int32_t)lroundf(celsius * 10.0f), 1, "°C");
}

/* ============================================================================
 *  静态界面
 * ========================================================================== */

static lv_obj_t *createLabel(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color,
                             int32_t x, int32_t y, int32_t w, int32_t h, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

static void createHalf(uint8_t side, int32_t offsetY)
{
    lv_obj_t *screen = lv_screen_active();

    // 标题栏
    lv_obj_t *header = lv_obj_create(screen);
    lv_obj_remove_style_all(header);
    lv_obj_set_pos(header, 0, offsetY);
    lv_obj_set_size(header, UI_SCREEN_W, UI_HEADER_H);
    lv_obj_set_style_bg_color(header, UI_COL_HEADER_BG, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);

    createLabel(header, cSideNames[side], UI_FONT_TITLE, UI_COL_VALUE, 8, 0, 140, UI_HEADER_H, LV_TEXT_ALIGN_LEFT);
    sStatusLabel[side] = createLabel(header, cStatusNames[UiStatus_NoData], UI_FONT_SMALL, UI_COL_OFF,
                                     104, 0, 128, UI_HEADER_H, LV_TEXT_ALIGN_RIGHT);
    strncpy(sStatusCache[side], cStatusNames[UiStatus_NoData], sizeof(sStatusCache[side]) - 1);

    // 5 行数值
    for (uint8_t i = 0; i < UI_ROW_COUNT; i++)
    {
        const int32_t y = offsetY + UI_ROW_FIRST_Y + (int32_t)i * UI_ROW_H;
        createLabel(screen, cRowNames[i], UI_FONT_SMALL, UI_COL_LABEL, UI_COL_LABEL_X, y, 72, UI_ROW_H,
                    LV_TEXT_ALIGN_LEFT);
        sValueLabel[side][i] = createLabel(screen, "--", UI_FONT_SMALL, UI_COL_VALUE, UI_COL_VALUE_X, y,
                                          UI_COL_VALUE_W, UI_ROW_H, LV_TEXT_ALIGN_RIGHT);
        strncpy(sValueCache[side][i], "--", sizeof(sValueCache[side][i]) - 1);
    }
}

void displayUiBegin()
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(screen, false);

    createHalf(0, 0);
    createHalf(1, UI_HALF2_Y);

    // 中间分隔线
    lv_obj_t *separator = lv_obj_create(screen);
    lv_obj_remove_style_all(separator);
    lv_obj_set_pos(separator, 0, UI_HALF_H);
    lv_obj_set_size(separator, UI_SCREEN_W, 2);
    lv_obj_set_style_bg_color(separator, UI_COL_SEPARATOR, 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
}

/* ============================================================================
 *  数值更新
 * ========================================================================== */

static void setLabelTextIfChanged(lv_obj_t *label, char *cache, size_t cacheSize, const char *text)
{
    if (strncmp(cache, text, cacheSize - 1) == 0)
        return;
    strncpy(cache, text, cacheSize - 1);
    cache[cacheSize - 1] = '\0';
    lv_label_set_text(label, text);
}

void displayUiSetSide(bool isAux, const PowerSupplyDisplayValues &v)
{
    const uint8_t side = isAux ? 1 : 0;
    if (sValueLabel[side][0] == nullptr)
        return;

    // ---- 状态 ----
    uint8_t status = UiStatus_NoData;
    if (v.valid)
        status = (v.fault != 0) ? UiStatus_Fault : (v.outputOn ? UiStatus_Ok : UiStatus_Off);

    lv_color_t statusColor = UI_COL_OFF;
    if (status == UiStatus_Ok)
        statusColor = UI_COL_OK;
    else if (status == UiStatus_Fault)
        statusColor = UI_COL_FAULT;

    if (strncmp(sStatusCache[side], cStatusNames[status], sizeof(sStatusCache[side]) - 1) != 0)
    {
        strncpy(sStatusCache[side], cStatusNames[status], sizeof(sStatusCache[side]) - 1);
        lv_label_set_text(sStatusLabel[side], cStatusNames[status]);
        lv_obj_set_style_text_color(sStatusLabel[side], statusColor, 0);
    }

    // ---- 数值 ----
    char buffer[24];
    for (uint8_t i = 0; i < UI_ROW_COUNT; i++)
    {
        if (!v.valid)
        {
            setLabelTextIfChanged(sValueLabel[side][i], sValueCache[side][i], sizeof(sValueCache[side][i]), "--");
            continue;
        }

        switch (i)
        {
            case 0: formatVoltage(buffer, sizeof(buffer), v.inputVoltage_mV); break;
            case 1: formatCurrent(buffer, sizeof(buffer), v.inputCurrent_mA); break;
            case 2: formatVoltage(buffer, sizeof(buffer), v.outputVoltage_mV); break;
            case 3: formatCurrent(buffer, sizeof(buffer), v.outputCurrent_mA); break;
            default: formatTemperature(buffer, sizeof(buffer), v.temperatureC); break;
        }
        setLabelTextIfChanged(sValueLabel[side][i], sValueCache[side][i], sizeof(sValueCache[side][i]), buffer);
    }
}

/* ============================================================================
 *  报警对话框
 * ========================================================================== */

bool displayUiAlarmVisible()
{
    return sAlarmRoot != nullptr;
}

static void createAlarmDialog()
{
    sAlarmRoot = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(sAlarmRoot);
    lv_obj_set_pos(sAlarmRoot, 0, 0);
    lv_obj_set_size(sAlarmRoot, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(sAlarmRoot, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(sAlarmRoot, LV_OPA_70, 0);
    lv_obj_set_scrollable(sAlarmRoot, false);

    lv_obj_t *card = lv_obj_create(sAlarmRoot);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 224, 156);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, UI_COL_DIALOG_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, UI_COL_DIALOG_BORDER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_scrollable(card, false);

    sAlarmTitle = createLabel(card, "电源报警", UI_FONT_TITLE, UI_COL_DIALOG_TITLE, 8, 6, 208, 24,
                              LV_TEXT_ALIGN_CENTER);

    sAlarmBody = lv_label_create(card);
    lv_label_set_text(sAlarmBody, "");
    lv_obj_set_style_text_font(sAlarmBody, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(sAlarmBody, UI_COL_VALUE, 0);
    lv_obj_set_style_text_align(sAlarmBody, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(sAlarmBody, 8, 38);
    lv_obj_set_size(sAlarmBody, 208, 86);
    lv_label_set_long_mode(sAlarmBody, LV_LABEL_LONG_WRAP);

    createLabel(card, "故障消失后自动关闭", UI_FONT_SMALL, UI_COL_DIALOG_HINT, 8, 126, 208, 22,
                LV_TEXT_ALIGN_CENTER);

    sAlarmBodyCache[0] = '\0';
}

static void destroyAlarmDialog()
{
    if (sAlarmRoot != nullptr)
        lv_obj_delete(sAlarmRoot);
    sAlarmRoot = nullptr;
    sAlarmTitle = nullptr;
    sAlarmBody = nullptr;
    sAlarmBodyCache[0] = '\0';
}

void displayUiSetAlarm(bool show, const char *title, const char *body)
{
    if (!show)
    {
        destroyAlarmDialog();
        return;
    }

    if (sAlarmRoot == nullptr)
        createAlarmDialog();

    if (title != nullptr && strncmp(lv_label_get_text(sAlarmTitle), title, 63) != 0)
        lv_label_set_text(sAlarmTitle, title);

    const char *text = (body != nullptr) ? body : "";
    if (strncmp(sAlarmBodyCache, text, sizeof(sAlarmBodyCache) - 1) != 0)
    {
        strncpy(sAlarmBodyCache, text, sizeof(sAlarmBodyCache) - 1);
        sAlarmBodyCache[sizeof(sAlarmBodyCache) - 1] = '\0';
        lv_label_set_text(sAlarmBody, text);
    }
}

void displayUiRefreshAll()
{
    lv_obj_invalidate(lv_screen_active());
    if (sAlarmRoot != nullptr)
        lv_obj_invalidate(sAlarmRoot);
}
