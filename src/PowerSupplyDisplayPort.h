#pragma once

#include <lvgl.h>
#include <stdint.h>

/* ============================================================================
 *  ST7789 (240x240, SPI) + LVGL 9.6 的移植层
 * ----------------------------------------------------------------------------
 *  引脚与参数全部来自 include/hardware.h 的 OPENKNX_BPS_DISPLAY_* 宏，
 *  本文件里没有任何硬编码的板级信息。
 *
 *  SPI 走 arduino-pico 的硬件 SPI（默认 SPI1），命令用轮询发送、
 *  像素数据用阻塞发送（本屏只是状态显示，不追求帧率，换来实现最简单可靠）。
 * ========================================================================== */

#ifndef OPENKNX_BPS_DISPLAY_FLUSH_LINES
    /// LVGL 绘制缓冲的行数（缓冲 = 240 x 本值 x 2 字节）。
    /// 240x40 = 19.2 KB；RAM 紧张时改小，想更流畅可以改大。
    #define OPENKNX_BPS_DISPLAY_FLUSH_LINES 40
#endif

/// 初始化 SPI + ST7789 + LVGL。重复调用只会执行一次。
/// @return true = 显示已建立；false = 初始化失败（SPI/参数问题）
bool displayPortBegin();

/// 背光亮度，0..100（%）。0 = 完全熄灭。
void displayPortSetBacklight(uint8_t percent);

/// 当前背光亮度（%）
uint8_t displayPortBacklight();

/// LVGL 显示对象；初始化失败时为 nullptr
lv_display_t *displayPortDisplay();

/// 面板颜色反转（多数 240x240 IPS ST7789 模块需要 true）。
/// 现象对不上时用控制台 `disp invert on|off` 现场切换。
void displayPortSetInvert(bool invert);
bool displayPortInvert();

/// 屏幕方向 0..3（0/90/180/270）。
void displayPortSetRotation(uint8_t rotation);
uint8_t displayPortRotation();
