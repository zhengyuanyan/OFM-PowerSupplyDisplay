#include "PowerSupplyDisplayPort.h"

#include "hardware.h"

#include <SPI.h>

/* ============================================================================
 *  板级默认值（可在编译时用 build_flags 覆盖）
 * ----------------------------------------------------------------------------
 *  面板对不上时按现象改：
 *    整屏反色（黑底变白底/照片底片效果）      -> OPENKNX_BPS_DISPLAY_INVERT 0
 *    红蓝互换（红色显示成蓝色）              -> OPENKNX_BPS_DISPLAY_BGR 1
 *    图像整体偏移若干像素                    -> OPENKNX_BPS_DISPLAY_GAP_X/Y
 *  前两个也可以开机后用控制台 `disp invert on|off` 现场试，确认后再固化成宏。
 * ========================================================================== */
#ifndef OPENKNX_BPS_DISPLAY_INVERT
    #define OPENKNX_BPS_DISPLAY_INVERT 1
#endif

#ifndef OPENKNX_BPS_DISPLAY_BGR
    #define OPENKNX_BPS_DISPLAY_BGR 0
#endif

#ifndef OPENKNX_BPS_DISPLAY_GAP_X
    #define OPENKNX_BPS_DISPLAY_GAP_X 0
#endif

#ifndef OPENKNX_BPS_DISPLAY_GAP_Y
    #define OPENKNX_BPS_DISPLAY_GAP_Y 0
#endif

/* ============================================================================
 *  内部状态
 * ========================================================================== */

static lv_display_t *sDisplay = nullptr;
static bool sPortReady = false;
static bool sInvert = OPENKNX_BPS_DISPLAY_INVERT;
static uint8_t sRotation = OPENKNX_BPS_DISPLAY_ROTATION;
static uint8_t sBacklight = 100;

// LVGL 绘制缓冲：240 x N 行 x 2 字节（RGB565）
static uint8_t sDrawBuf[OPENKNX_BPS_DISPLAY_H_RES * OPENKNX_BPS_DISPLAY_FLUSH_LINES * 2]
    __attribute__((aligned(4)));

static SPISettings const sSpiSettings(OPENKNX_BPS_DISPLAY_SPI_HZ, MSBFIRST, SPI_MODE0);

/* ============================================================================
 *  LVGL 回调
 * ========================================================================== */

static uint32_t lvglTickCb()
{
    return (uint32_t)millis();
}

/// CS/DC 的公共起手式：拉 CS、切 DC
static inline void cmdBegin(bool dataMode)
{
    digitalWrite(OPENKNX_BPS_DISPLAY_CS_PIN, LOW);
    digitalWrite(OPENKNX_BPS_DISPLAY_DC_PIN, dataMode ? HIGH : LOW);
}

static inline void cmdEnd()
{
    digitalWrite(OPENKNX_BPS_DISPLAY_CS_PIN, HIGH);
}

/**
 * @brief 发命令（+ 参数）。LVGL 的 MIPI 驱动用这个发初始化序列和寄存器写入。
 */
static void lcdSendCmd(lv_display_t *disp, const uint8_t *cmd, size_t cmdSize, const uint8_t *param, size_t paramSize)
{
    (void)disp;
    OPENKNX_BPS_DISPLAY_SPI.beginTransaction(sSpiSettings);
    if (cmdSize > 0)
    {
        cmdBegin(false);
        OPENKNX_BPS_DISPLAY_SPI.transfer(cmd, nullptr, cmdSize);
        cmdEnd();
    }
    if (paramSize > 0)
    {
        cmdBegin(true);
        OPENKNX_BPS_DISPLAY_SPI.transfer(param, nullptr, paramSize);
        cmdEnd();
    }
    OPENKNX_BPS_DISPLAY_SPI.endTransaction();
}

/**
 * @brief 发像素数据。
 *
 * ST7789 在 SPI 上要求 **大端** 的 RGB565，而 LVGL 的缓冲是本机字节序
 * (小端)，所以这里就地做一次 16 bit 字节交换再发。
 *
 * 本实现是阻塞发送，发完立即调用 lv_display_flush_ready()，
 * 因此 LVGL 可以马上复用这块缓冲（不需要双缓冲/DMA）。
 */
static void lcdSendColor(lv_display_t *disp, const uint8_t *cmd, size_t cmdSize, uint8_t *param, size_t paramSize)
{
    if (param != nullptr && paramSize >= 2)
        lv_draw_rgb565_swap(param, (uint32_t)(paramSize / 2));

    OPENKNX_BPS_DISPLAY_SPI.beginTransaction(sSpiSettings);
    if (cmdSize > 0)
    {
        cmdBegin(false);
        OPENKNX_BPS_DISPLAY_SPI.transfer(cmd, nullptr, cmdSize);
        cmdEnd();
    }
    if (paramSize > 0)
    {
        cmdBegin(true);
        OPENKNX_BPS_DISPLAY_SPI.transfer(param, nullptr, paramSize);
        cmdEnd();
    }
    OPENKNX_BPS_DISPLAY_SPI.endTransaction();

    lv_display_flush_ready(disp);
}

/* ============================================================================
 *  背光
 * ========================================================================== */

void displayPortSetBacklight(uint8_t percent)
{
    if (percent > 100)
        percent = 100;
    sBacklight = percent;

    const uint8_t duty = (uint8_t)((uint16_t)percent * 255u / 100u);
    const bool activeHigh = (OPENKNX_BPS_DISPLAY_BL_ACTIVE_ON) != 0;
    analogWrite(OPENKNX_BPS_DISPLAY_BL_PIN, activeHigh ? duty : (255u - duty));
}

uint8_t displayPortBacklight()
{
    return sBacklight;
}

/* ============================================================================
 *  面板参数
 * ========================================================================== */

void displayPortSetInvert(bool invert)
{
    sInvert = invert;
    if (sDisplay != nullptr)
        lv_st7789_set_invert(sDisplay, sInvert);
}

bool displayPortInvert()
{
    return sInvert;
}

void displayPortSetRotation(uint8_t rotation)
{
    sRotation = (uint8_t)(rotation & 0x03);
    if (sDisplay != nullptr)
        lv_display_set_rotation(sDisplay, (lv_display_rotation_t)sRotation);
}

uint8_t displayPortRotation()
{
    return sRotation;
}

lv_display_t *displayPortDisplay()
{
    return sDisplay;
}

/* ============================================================================
 *  初始化
 * ========================================================================== */

bool displayPortBegin()
{
    if (sPortReady)
        return sDisplay != nullptr;

    // ---- 普通 GPIO ----
    pinMode(OPENKNX_BPS_DISPLAY_CS_PIN, OUTPUT);
    digitalWrite(OPENKNX_BPS_DISPLAY_CS_PIN, HIGH);
    pinMode(OPENKNX_BPS_DISPLAY_DC_PIN, OUTPUT);
    digitalWrite(OPENKNX_BPS_DISPLAY_DC_PIN, LOW);

#if (OPENKNX_BPS_DISPLAY_RST_PIN) >= 0
    pinMode(OPENKNX_BPS_DISPLAY_RST_PIN, OUTPUT);
    // 给面板一个复位脉冲（RST 直接接板级复位时可以省略）
    digitalWrite(OPENKNX_BPS_DISPLAY_RST_PIN, LOW);
    delay(20);
    digitalWrite(OPENKNX_BPS_DISPLAY_RST_PIN, HIGH);
    delay(120);
#endif

    // ---- 背光 ----
    // 注意：arduino-pico 的 analogWriteFreq() 是**全局**的，OAM 里的蜂鸣器模块
    // （OFM-BuzzerAlert）正在用它做发声频率，所以这里绝不要去改。
    // 两者本来就在不同的 PWM slice 上（GPIO21 -> slice2，蜂鸣器 GPIO29 -> slice6），
    // 各用自己初始化时的频率，互不影响；背光用内核默认的 1 kHz 已足够（LED 不会闪）。
    pinMode(OPENKNX_BPS_DISPLAY_BL_PIN, OUTPUT);
    digitalWrite(OPENKNX_BPS_DISPLAY_BL_PIN, LOW);
    displayPortSetBacklight(sBacklight);

    // ---- SPI ----
    // 注意：CS 由固件自己拉，所以这里不交给硬件，begin(false) 即可
    OPENKNX_BPS_DISPLAY_SPI.setSCK(OPENKNX_BPS_DISPLAY_SCK_PIN);
    OPENKNX_BPS_DISPLAY_SPI.setTX(OPENKNX_BPS_DISPLAY_MOSI_PIN);
    OPENKNX_BPS_DISPLAY_SPI.begin(false);

    // ---- LVGL ----
    lv_init();
    lv_tick_set_cb(lvglTickCb);

    const lv_lcd_flag_t flags = (OPENKNX_BPS_DISPLAY_BGR) ? LV_LCD_FLAG_BGR : LV_LCD_FLAG_NONE;
    sDisplay = lv_st7789_create(OPENKNX_BPS_DISPLAY_H_RES, OPENKNX_BPS_DISPLAY_V_RES, flags,
                                lcdSendCmd, lcdSendColor);
    if (sDisplay == nullptr)
        return false;

#if (OPENKNX_BPS_DISPLAY_GAP_X) != 0 || (OPENKNX_BPS_DISPLAY_GAP_Y) != 0
    lv_st7789_set_gap(sDisplay, OPENKNX_BPS_DISPLAY_GAP_X, OPENKNX_BPS_DISPLAY_GAP_Y);
#endif

    lv_st7789_set_invert(sDisplay, sInvert);
    lv_display_set_rotation(sDisplay, (lv_display_rotation_t)sRotation);

    lv_display_set_buffers(sDisplay, sDrawBuf, nullptr, sizeof(sDrawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_default(sDisplay);

    sPortReady = true;
    return true;
}
