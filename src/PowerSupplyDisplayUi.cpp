#include "PowerSupplyDisplayUi.h"

#include "PowerSupplyDisplayFonts.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 *  布局常量（单位 px，屏幕 240x240）
 *
 *    ┌──┬──────────────────────────────┐
 *    │A │ 总线电源            正常 ●   │  y=1  .. 118   卡片（含标题栏 22）
 *    ├──┼──────────────────────────────┤
 *    │B │ 辅助电源            已关闭 ● │  y=121 .. 239
 *    └──┴──────────────────────────────┘
 *
 *  左侧 A/B 列宽 UI_LEFT_COL_W，卡片内：标题栏 22 + 5 行 x 19 = 117（<= 118）
 *  正在供电的那一路整格底色变绿。
 * ========================================================================== */
#define UI_SCREEN_W 240
#define UI_SCREEN_H 240

// 卡片（上下两半）
#define UI_CARD_X 32
#define UI_CARD_W (UI_SCREEN_W - UI_CARD_X - 2)
#define UI_CARD_H 118
#define UI_CARD_Y_BUS 1
#define UI_CARD_Y_AUX 121
#define UI_CARD_RADIUS 10

#define UI_HEADER_H 22
#define UI_ROW_H 19
#define UI_ROW_FIRST_Y 23
#define UI_ROW_COUNT 5

// 左侧 A/B 输入电源状态列（向内留 3px 边距，圆角显示）
#define UI_LEFT_COL_W 30
#define UI_INPUT_X 3
#define UI_INPUT_W (UI_LEFT_COL_W - 6)
#define UI_INPUT_RADIUS 9

// 卡片内部（相对卡片）的列位置
#define UI_COL_LABEL_X 6
#define UI_COL_LABEL_W 62
#define UI_COL_VALUE_X 70
#define UI_COL_VALUE_W (UI_CARD_W - UI_COL_VALUE_X - 6)

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
#define UI_COL_BG lv_color_hex(0x0A0E14)          // 屏幕底色（近黑的深蓝）
#define UI_COL_CARD_BG lv_color_hex(0x11161F)     // 卡片底色
#define UI_COL_CARD_BORDER lv_color_hex(0x1F2937) // 卡片描边
#define UI_COL_HEADER_BG lv_color_hex(0x18202C)   // 标题栏底色
#define UI_COL_ROW_LINE lv_color_hex(0x1A2230)    // 行分隔线
#define UI_COL_LABEL lv_color_hex(0x8FA3B8)       // 行标签
#define UI_COL_VALUE lv_color_hex(0xF2F6FA)       // 数值
#define UI_COL_VALUE_DIM lv_color_hex(0x5A6675)   // 无数值 / 输出关闭时的数值
#define UI_COL_ACCENT_BUS lv_color_hex(0x4C8DFF)  // 总线侧强调色（蓝）
#define UI_COL_ACCENT_AUX lv_color_hex(0xB07CFF)  // 辅助侧强调色（紫）
#define UI_COL_OK lv_color_hex(0x39D353)
#define UI_COL_OFF lv_color_hex(0x8A8A8A)
#define UI_COL_FAULT lv_color_hex(0xFF4D4D)

// 报警对话框
#define UI_COL_DIALOG_BG lv_color_hex(0x2A0B0B)
#define UI_COL_DIALOG_BAND lv_color_hex(0x5A1616)
#define UI_COL_DIALOG_BORDER lv_color_hex(0xFF3B30)
#define UI_COL_DIALOG_HINT lv_color_hex(0xFFB0B0)

// 左侧 A/B 输入电源状态列
#define UI_COL_INPUT_ON_BG lv_color_hex(0x12893F)
#define UI_COL_INPUT_ON_BG2 lv_color_hex(0x1FB356) // 渐变下端
#define UI_COL_INPUT_ON_BORDER lv_color_hex(0x2EE56E)
#define UI_COL_INPUT_ON_TEXT lv_color_hex(0xFFFFFF)
#define UI_COL_INPUT_OFF_BG lv_color_hex(0x151B24)
#define UI_COL_INPUT_OFF_BORDER lv_color_hex(0x243040)
#define UI_COL_INPUT_OFF_TEXT lv_color_hex(0x5A6675)

// 开机界面
#define UI_COL_BOOT_GREEN lv_color_hex(0x4CB944) // OpenKNX 标志里的绿色

static lv_obj_t *sValueLabel[2][UI_ROW_COUNT] = {{nullptr}};
static lv_obj_t *sStatusLabel[2] = {nullptr};
static lv_obj_t *sStatusDot[2] = {nullptr};
static char sValueCache[2][UI_ROW_COUNT][24] = {{{0}}};
static char sStatusCache[2][16] = {{0}};
static uint8_t sValueTone[2][UI_ROW_COUNT] = {{0}}; // 0 = 暗, 1 = 亮（避免重复设置样式）

// 左侧 A/B 输入电源状态格（索引 0 = A 路，1 = B 路）
static lv_obj_t *sInputBox[2] = {nullptr};
static lv_obj_t *sInputLabel[2] = {nullptr};
static bool sInputOn[2] = {false, false};
static bool sInputValid = false;

static lv_obj_t *sAlarmRoot = nullptr;
static lv_obj_t *sAlarmTitle = nullptr;
static lv_obj_t *sAlarmBody = nullptr;
static char sAlarmBodyCache[256] = {0};

// 开机界面（显示在主屏幕上；主界面在它结束后才建立）
static lv_obj_t *sBootRoot = nullptr;
static lv_obj_t *sBootChipDot[DISPLAY_BOOT_CHIP_COUNT] = {nullptr};
static lv_obj_t *sBootChipState[DISPLAY_BOOT_CHIP_COUNT] = {nullptr};
static lv_obj_t *sBootProgressFill = nullptr;
static lv_obj_t *sBootFooter = nullptr;
static DisplayBootState sBootChipShown[DISPLAY_BOOT_CHIP_COUNT] = {DisplayBoot_Pending};
static uint8_t sBootProgressShown = 0xFF;
static char sBootFooterCache[32] = {0};

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

/// 圆角实心块（用作卡片 / 强调条 / 状态点 / 行分隔线）
static lv_obj_t *createBlock(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t color,
                             int32_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_scrollable(obj, false);
    return obj;
}

static void createHalf(uint8_t side, int32_t cardY)
{
    lv_obj_t *screen = lv_screen_active();
    const lv_color_t accent = (side == 0) ? UI_COL_ACCENT_BUS : UI_COL_ACCENT_AUX;

    // 卡片：深色圆角 + 细描边
    lv_obj_t *card = lv_obj_create(screen);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, UI_CARD_X, cardY);
    lv_obj_set_size(card, UI_CARD_W, UI_CARD_H);
    lv_obj_set_style_bg_color(card, UI_COL_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, UI_CARD_RADIUS, 0);
    lv_obj_set_style_border_color(card, UI_COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_scrollable(card, false);

    // 标题栏：略亮的底色 + 顶部同色圆角（盖住卡片上圆角）
    lv_obj_t *header = lv_obj_create(card);
    lv_obj_remove_style_all(header);
    lv_obj_set_pos(header, 1, 1);
    lv_obj_set_size(header, UI_CARD_W - 2, UI_HEADER_H);
    lv_obj_set_style_bg_color(header, UI_COL_HEADER_BG, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(header, UI_CARD_RADIUS - 1, 0);
    lv_obj_set_scrollable(header, false);

    // 强调条：这一侧的主题色
    createBlock(header, 4, 6, 3, 10, accent, 2);

    createLabel(header, cSideNames[side], UI_FONT_TITLE, UI_COL_VALUE, 12, 0, 100, UI_HEADER_H,
                LV_TEXT_ALIGN_LEFT);

    // 状态文字（右对齐）+ 前面的小圆点
    sStatusLabel[side] = createLabel(header, cStatusNames[UiStatus_NoData], UI_FONT_SMALL, UI_COL_OFF,
                                     UI_CARD_W - 66, 0, 56, UI_HEADER_H, LV_TEXT_ALIGN_RIGHT);
    strncpy(sStatusCache[side], cStatusNames[UiStatus_NoData], sizeof(sStatusCache[side]) - 1);
    sStatusDot[side] = createBlock(header, UI_CARD_W - 78, 7, 8, 8, UI_COL_OFF, LV_RADIUS_CIRCLE);

    // 5 行数值（行之间一条极淡的分隔线）
    for (uint8_t i = 0; i < UI_ROW_COUNT; i++)
    {
        const int32_t y = UI_ROW_FIRST_Y + (int32_t)i * UI_ROW_H;

        if (i > 0)
            createBlock(card, 6, y - 1, UI_CARD_W - 12, 1, UI_COL_ROW_LINE, 0);

        createLabel(card, cRowNames[i], UI_FONT_SMALL, UI_COL_LABEL, UI_COL_LABEL_X, y, UI_COL_LABEL_W, UI_ROW_H,
                    LV_TEXT_ALIGN_LEFT);
        sValueLabel[side][i] = createLabel(card, "--", UI_FONT_SMALL, UI_COL_VALUE_DIM, UI_COL_VALUE_X, y,
                                          UI_COL_VALUE_W, UI_ROW_H, LV_TEXT_ALIGN_RIGHT);
        strncpy(sValueCache[side][i], "--", sizeof(sValueCache[side][i]) - 1);
    }
}

/**
 * @brief 左侧 A/B 输入电源状态列：一列向下分成两格，圆角 + 描边
 *
 * A / B 是两路输入电源（不是总线/辅助通道），两格只是排版上与上下卡片对齐；
 * 正在供电的那一格底色变绿（带渐变与高亮描边），未供电为深灰。
 */
static void createInputColumn()
{
    lv_obj_t *screen = lv_screen_active();
    static const char *const names[2] = {"A", "B"};
    const int32_t ys[2] = {UI_CARD_Y_BUS, UI_CARD_Y_AUX};

    for (uint8_t i = 0; i < 2; i++)
    {
        lv_obj_t *box = lv_obj_create(screen);
        lv_obj_remove_style_all(box);
        lv_obj_set_pos(box, UI_INPUT_X, ys[i]);
        lv_obj_set_size(box, UI_INPUT_W, UI_CARD_H);
        lv_obj_set_style_bg_color(box, UI_COL_INPUT_OFF_BG, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(box, UI_INPUT_RADIUS, 0);
        lv_obj_set_style_border_color(box, UI_COL_INPUT_OFF_BORDER, 0);
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_scrollable(box, false);
        sInputBox[i] = box;

        sInputLabel[i] = createLabel(box, names[i], UI_FONT_TITLE, UI_COL_INPUT_OFF_TEXT, 0, 0, UI_INPUT_W,
                                     UI_HEADER_H, LV_TEXT_ALIGN_CENTER);
        lv_obj_center(sInputLabel[i]); // 横向居中 + 纵向居中
    }
}

void displayUiBegin()
{
    // 主界面只在开机界面结束后建立；重播开机界面时不要重建（保留原有数值）
    if (sValueLabel[0][0] != nullptr)
    {
        lv_obj_invalidate(lv_screen_active());
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(screen, false);

    createHalf(0, UI_CARD_Y_BUS);
    createHalf(1, UI_CARD_Y_AUX);
    createInputColumn();
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
        lv_obj_set_style_bg_color(sStatusDot[side], statusColor, 0);
    }

    // ---- 数值（无数据 / 输出关闭时用暗色，正常时亮白）----
    const uint8_t tone = (v.valid && v.outputOn) ? 1 : 0;
    char buffer[24];
    for (uint8_t i = 0; i < UI_ROW_COUNT; i++)
    {
        if (tone != sValueTone[side][i])
        {
            sValueTone[side][i] = tone;
            lv_obj_set_style_text_color(sValueLabel[side][i], tone ? UI_COL_VALUE : UI_COL_VALUE_DIM, 0);
        }

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

void displayUiSetInputPower(bool aOn, bool bOn)
{
    if (sInputBox[0] == nullptr)
        return;

    const bool on[2] = {aOn, bOn};
    for (uint8_t i = 0; i < 2; i++)
    {
        // 第一次调用时强制刷一次，之后只在状态变化时碰 LVGL
        if (sInputValid && sInputOn[i] == on[i])
            continue;

        sInputOn[i] = on[i];
        lv_obj_set_style_bg_color(sInputBox[i], on[i] ? UI_COL_INPUT_ON_BG : UI_COL_INPUT_OFF_BG, 0);
        lv_obj_set_style_bg_grad_color(sInputBox[i], on[i] ? UI_COL_INPUT_ON_BG2 : UI_COL_INPUT_OFF_BG, 0);
        lv_obj_set_style_bg_grad_dir(sInputBox[i], LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_color(sInputBox[i], on[i] ? UI_COL_INPUT_ON_BORDER : UI_COL_INPUT_OFF_BORDER, 0);
        lv_obj_set_style_text_color(sInputLabel[i], on[i] ? UI_COL_INPUT_ON_TEXT : UI_COL_INPUT_OFF_TEXT, 0);
    }
    sInputValid = true;
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
    lv_obj_set_size(card, 220, 152);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, UI_COL_DIALOG_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, UI_COL_DIALOG_BORDER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_shadow_width(card, 16, 0);
    lv_obj_set_style_shadow_color(card, UI_COL_DIALOG_BORDER, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    lv_obj_set_scrollable(card, false);

    // 顶部标题带（内缩圆角）+ 白色标题，让弹窗更像"告警卡片"
    createBlock(card, 3, 3, 214, 26, UI_COL_DIALOG_BAND, 9);
    sAlarmTitle = createLabel(card, "电源报警", UI_FONT_TITLE, UI_COL_VALUE, 8, 3, 204, 26, LV_TEXT_ALIGN_CENTER);

    sAlarmBody = lv_label_create(card);
    lv_label_set_text(sAlarmBody, "");
    lv_obj_set_style_text_font(sAlarmBody, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(sAlarmBody, UI_COL_VALUE, 0);
    lv_obj_set_style_text_align(sAlarmBody, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(sAlarmBody, 10, 38);
    lv_obj_set_size(sAlarmBody, 200, 82);
    lv_label_set_long_mode(sAlarmBody, LV_LABEL_LONG_WRAP);

    createLabel(card, "故障消失后自动关闭", UI_FONT_SMALL, UI_COL_DIALOG_HINT, 8, 122, 204, 22,
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
    if (sBootRoot != nullptr)
        lv_obj_invalidate(sBootRoot);
}

/* ============================================================================
 *  开机界面
 *
 *    ┌──────────────────────────────────────┐
 *    │ Open              SC8815             │  左上 OpenKNX 标志（Open / 绿线 / KNX）
 *    │ ────                                  │  右侧设备名 + 中文字幕
 *    │ KNX               电源显示            │
 *    ├──────────────────────────────────────┤
 *    │ 芯片初始化                            │
 *    │ ┌──────────────────────────────────┐ │
 *    │ │ ● SC8815 总线              OK    │ │  6 行 = 两路 × SC8815 / INA238 / TMP102
 *    │ │ ● INA238 总线              OK    │ │  检测中 = 灰，成功 = 绿，失败 = 红
 *    │ │ ● TMP102 总线              失败  │ │
 *    │ │ ...                              │ │
 *    │ └──────────────────────────────────┘ │
 *    │ ▓▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░░░░░░░ │  进度条
 *    │            正在检查芯片               │
 *    └──────────────────────────────────────┘
 * ========================================================================== */

#define UI_BOOT_LOGO_X 12
#define UI_BOOT_LOGO_Y 10
#define UI_BOOT_LOGO_W 72
#define UI_BOOT_TITLE_X 94
#define UI_BOOT_DIVIDER_Y 66

#define UI_BOOT_LIST_X 8
#define UI_BOOT_LIST_Y 92
#define UI_BOOT_LIST_W (UI_SCREEN_W - UI_BOOT_LIST_X - 8)
#define UI_BOOT_LIST_H 118
#define UI_BOOT_ROW_Y 2
#define UI_BOOT_DOT_X 8
#define UI_BOOT_DOT_SIZE 9
#define UI_BOOT_NAME_X 22
#define UI_BOOT_NAME_W 106
#define UI_BOOT_STATE_X 136
#define UI_BOOT_STATE_W (UI_BOOT_LIST_W - UI_BOOT_STATE_X - 8)

#define UI_BOOT_PROGRESS_X 10
#define UI_BOOT_PROGRESS_Y 214
#define UI_BOOT_PROGRESS_H 6
#define UI_BOOT_PROGRESS_W (UI_SCREEN_W - 2 * UI_BOOT_PROGRESS_X)
#define UI_BOOT_FOOTER_Y 221

static const char *const cBootChipNames[DISPLAY_BOOT_CHIP_COUNT] = {
    "SC8815 总线", "INA238 总线", "TMP102 总线",
    "SC8815 辅助", "INA238 辅助", "TMP102 辅助",
};

static const char *const cBootStateText[] = {"检测中", "OK", "失败"};

static lv_color_t bootStateColor(DisplayBootState state)
{
    if (state == DisplayBoot_Ok)
        return UI_COL_OK;
    if (state == DisplayBoot_Fail)
        return UI_COL_FAULT;
    return UI_COL_VALUE_DIM;
}

void displayUiBeginBoot()
{
    displayUiFinishBoot();

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_scrollable(screen, false);

    sBootRoot = lv_obj_create(screen);
    lv_obj_remove_style_all(sBootRoot);
    lv_obj_set_pos(sBootRoot, 0, 0);
    lv_obj_set_size(sBootRoot, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(sBootRoot, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(sBootRoot, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(sBootRoot, false);

    // ---- OpenKNX 标志：Open / 绿色总线 / KNX（画在深色底上，文字用白色）----
    createLabel(sBootRoot, "Open", UI_FONT_SMALL, UI_COL_VALUE, UI_BOOT_LOGO_X, UI_BOOT_LOGO_Y, UI_BOOT_LOGO_W,
                UI_ROW_H, LV_TEXT_ALIGN_CENTER);
    createBlock(sBootRoot, UI_BOOT_LOGO_X + 6, UI_BOOT_LOGO_Y + 19, UI_BOOT_LOGO_W - 12, 2, UI_COL_BOOT_GREEN, 1);
    createBlock(sBootRoot, UI_BOOT_LOGO_X, UI_BOOT_LOGO_Y + 16, 6, 6, UI_COL_BOOT_GREEN, 1);
    createBlock(sBootRoot, UI_BOOT_LOGO_X + UI_BOOT_LOGO_W - 6, UI_BOOT_LOGO_Y + 16, 6, 6, UI_COL_BOOT_GREEN, 1);
    createLabel(sBootRoot, "KNX", UI_FONT_TITLE, UI_COL_VALUE, UI_BOOT_LOGO_X, UI_BOOT_LOGO_Y + 23, UI_BOOT_LOGO_W,
                24, LV_TEXT_ALIGN_CENTER);

    createLabel(sBootRoot, "SC8815", UI_FONT_TITLE, UI_COL_VALUE, UI_BOOT_TITLE_X, UI_BOOT_LOGO_Y - 1, 140, 24,
                LV_TEXT_ALIGN_LEFT);
    createLabel(sBootRoot, "电源显示", UI_FONT_SMALL, UI_COL_ACCENT_BUS, UI_BOOT_TITLE_X, UI_BOOT_LOGO_Y + 23, 140,
                UI_ROW_H, LV_TEXT_ALIGN_LEFT);

    createBlock(sBootRoot, UI_BOOT_PROGRESS_X, UI_BOOT_DIVIDER_Y, UI_BOOT_PROGRESS_W, 1, UI_COL_ROW_LINE, 0);
    createLabel(sBootRoot, "芯片初始化", UI_FONT_SMALL, UI_COL_ACCENT_BUS, UI_BOOT_PROGRESS_X + 2,
                UI_BOOT_DIVIDER_Y + 5, 160, UI_ROW_H, LV_TEXT_ALIGN_LEFT);

    // ---- 芯片状态列表 ----
    lv_obj_t *card = lv_obj_create(sBootRoot);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, UI_BOOT_LIST_X, UI_BOOT_LIST_Y);
    lv_obj_set_size(card, UI_BOOT_LIST_W, UI_BOOT_LIST_H);
    lv_obj_set_style_bg_color(card, UI_COL_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, UI_CARD_RADIUS, 0);
    lv_obj_set_style_border_color(card, UI_COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_scrollable(card, false);

    for (uint8_t i = 0; i < DISPLAY_BOOT_CHIP_COUNT; i++)
    {
        const int32_t y = UI_BOOT_ROW_Y + i * UI_ROW_H;

        sBootChipDot[i] = createBlock(card, UI_BOOT_DOT_X, y + 5, UI_BOOT_DOT_SIZE, UI_BOOT_DOT_SIZE, UI_COL_VALUE_DIM,
                                     LV_RADIUS_CIRCLE);
        createLabel(card, cBootChipNames[i], UI_FONT_SMALL, UI_COL_VALUE, UI_BOOT_NAME_X, y, UI_BOOT_NAME_W,
                    UI_ROW_H, LV_TEXT_ALIGN_LEFT);
        sBootChipState[i] = createLabel(card, cBootStateText[DisplayBoot_Pending], UI_FONT_SMALL, UI_COL_VALUE_DIM,
                                        UI_BOOT_STATE_X, y, UI_BOOT_STATE_W, UI_ROW_H, LV_TEXT_ALIGN_RIGHT);
        sBootChipShown[i] = DisplayBoot_Pending;
    }

    // ---- 进度条 ----
    createBlock(sBootRoot, UI_BOOT_PROGRESS_X, UI_BOOT_PROGRESS_Y, UI_BOOT_PROGRESS_W, UI_BOOT_PROGRESS_H,
                UI_COL_ROW_LINE, 3);
    sBootProgressFill = createBlock(sBootRoot, UI_BOOT_PROGRESS_X, UI_BOOT_PROGRESS_Y, 0, UI_BOOT_PROGRESS_H,
                                    UI_COL_ACCENT_BUS, 3);
    sBootProgressShown = 0xFF;

    // ---- 底部总结 ----
    sBootFooter = createLabel(sBootRoot, "正在检查芯片", UI_FONT_SMALL, UI_COL_VALUE_DIM, UI_BOOT_PROGRESS_X,
                              UI_BOOT_FOOTER_Y, UI_BOOT_PROGRESS_W, UI_ROW_H, LV_TEXT_ALIGN_CENTER);
    strncpy(sBootFooterCache, "正在检查芯片", sizeof(sBootFooterCache) - 1);

    lv_obj_invalidate(sBootRoot);
}

void displayUiSetBootChip(uint8_t index, DisplayBootState state)
{
    if (sBootRoot == nullptr || index >= DISPLAY_BOOT_CHIP_COUNT)
        return;
    if (sBootChipShown[index] == state)
        return;

    sBootChipShown[index] = state;
    const lv_color_t color = bootStateColor(state);
    lv_label_set_text(sBootChipState[index], cBootStateText[state]);
    lv_obj_set_style_text_color(sBootChipState[index], color, 0);
    lv_obj_set_style_bg_color(sBootChipDot[index], color, 0);

    bool allDone = true;
    bool anyFail = false;
    for (uint8_t i = 0; i < DISPLAY_BOOT_CHIP_COUNT; i++)
    {
        if (sBootChipShown[i] == DisplayBoot_Pending)
            allDone = false;
        else if (sBootChipShown[i] == DisplayBoot_Fail)
            anyFail = true;
    }

    if (allDone && sBootFooter != nullptr)
    {
        const char *text = anyFail ? "初始化完成，有故障" : "初始化完成";
        if (strncmp(sBootFooterCache, text, sizeof(sBootFooterCache) - 1) != 0)
        {
            strncpy(sBootFooterCache, text, sizeof(sBootFooterCache) - 1);
            sBootFooterCache[sizeof(sBootFooterCache) - 1] = '\0';
            lv_label_set_text(sBootFooter, text);
        }
        lv_obj_set_style_text_color(sBootFooter, anyFail ? UI_COL_FAULT : UI_COL_OK, 0);
    }
}

void displayUiSetBootProgress(uint8_t percent)
{
    if (sBootProgressFill == nullptr || percent > 100 || percent == sBootProgressShown)
        return;

    sBootProgressShown = percent;
    const int32_t width = (UI_BOOT_PROGRESS_W * percent) / 100;
    lv_obj_set_width(sBootProgressFill, width);
    lv_obj_set_style_radius(sBootProgressFill, (width > 0 && width < UI_BOOT_PROGRESS_H) ? width / 2 : 3, 0);
}

void displayUiFinishBoot()
{
    if (sBootRoot != nullptr)
        lv_obj_delete(sBootRoot);

    sBootRoot = nullptr;
    sBootProgressFill = nullptr;
    sBootFooter = nullptr;
    for (uint8_t i = 0; i < DISPLAY_BOOT_CHIP_COUNT; i++)
    {
        sBootChipDot[i] = nullptr;
        sBootChipState[i] = nullptr;
        sBootChipShown[i] = DisplayBoot_Pending;
    }
    sBootProgressShown = 0xFF;
    sBootFooterCache[0] = '\0';
}

bool displayUiBootVisible()
{
    return sBootRoot != nullptr;
}
