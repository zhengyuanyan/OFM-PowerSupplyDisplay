#pragma once

#include <lvgl.h>
#include <stdint.h>

/* ============================================================================
 *  240x240 界面：上下各半屏 + 左侧 A/B 输入电源列 + 报警对话框
 * ----------------------------------------------------------------------------
 *  上半屏 = 总线电源，下半屏 = 辅助电源，每半屏显示：
 *      输入电压 / 输入电流   (SC8815 ADIN + IBAT)
 *      输出电压 / 输出电流   (输出侧 INA238)
 *      温度                 (TMP102)
 *  最左边一列向下分两格显示 A / B 路输入电源，正在供电的那一格底色变绿。
 *  报警时在 lv_layer_top() 上弹出一个红色对话框（不改变底层数值显示）。
 *  上电时先显示开机界面（OpenKNX 图标 + 芯片初始化状态）；开机界面结束后
 *  才创建并显示数值界面（主界面对象在此之前不存在）。
 *
 *  本文件只做界面，不碰硬件、不读参数：数据由 PowerSupplyDisplayModule 推进来。
 * ========================================================================== */

/// 一侧（总线/辅助）要显示的数值
struct PowerSupplyDisplayValues
{
    bool valid = false;             ///< 是否已收到过数据
    bool chipOk = false;            ///< SC8815 初始化是否成功
    bool inaOk = false;             ///< INA238 初始化是否成功
    bool tempOk = false;            ///< TMP102 初始化是否成功
    bool outputOn = false;          ///< 输出是否接通（PGATE）
    uint8_t fault = 0;              ///< 故障位（OpenKNX BpsFault 的位定义）
    float inputVoltage_mV = 0.0f;   ///< SC8815 ADIN 换算后的输入电压
    float inputCurrent_mA = 0.0f;   ///< SC8815 IBAT
    float outputVoltage_mV = 0.0f;  ///< INA238 输出电压
    float outputCurrent_mA = 0.0f;  ///< INA238 输出电流
    float temperatureC = 0.0f;      ///< TMP102 温度
};

/// 建立数值界面（主界面）。开机界面结束后才调用；重复调用只触发重绘。
void displayUiBegin();

/// 更新一侧的数值显示（内部做了"值没变就不碰 LVGL"的判断）
void displayUiSetSide(bool isAux, const PowerSupplyDisplayValues &values);

/// 更新左侧 A/B 输入电源状态列：true = 该路正在供电（整格变绿）
/// 没有任何数据前调用一次即可保持后续状态刷新
void displayUiSetInputPower(bool aOn, bool bOn);

/// 报警对话框：show=true 时弹出/刷新内容
/// @param body 多行文本（\n 分隔），nullptr 或空串表示只显示标题
void displayUiSetAlarm(bool show, const char *title, const char *body);

/// 报警对话框当前是否可见
bool displayUiAlarmVisible();

/* ============================================================================
 *  开机界面（OpenKNX 图标 + 各芯片初始化状态）
 * ----------------------------------------------------------------------------
 *  开机界面直接显示在主屏幕上；走完后调用方再建立数值界面（displayUiBegin），
 *  因此主界面在开机阶段根本不存在。
 * ========================================================================== */

/// 开机界面上一共列出的芯片数（两路 × SC8815 / INA238 / TMP102）
#define DISPLAY_BOOT_CHIP_COUNT 6

/// 一颗芯片的初始化状态
enum DisplayBootState : uint8_t
{
    DisplayBoot_Pending = 0, ///< 还在检测
    DisplayBoot_Ok,          ///< 初始化成功
    DisplayBoot_Fail,        ///< 初始化失败（无应答）
};

/// 创建（或重新创建）开机界面并覆盖整个屏幕
void displayUiBeginBoot();

/// 更新某一颗芯片的初始化状态（index 见 .cpp 里的 cBootChipNames 顺序）
void displayUiSetBootChip(uint8_t index, DisplayBootState state);

/// 开机界面底部进度条，0..100
void displayUiSetBootProgress(uint8_t percent);

/// 关闭开机界面（删除其对象；随后由调用方建立主界面）
void displayUiFinishBoot();

/// 开机界面当前是否可见
bool displayUiBootVisible();

/// 颜色反转/方向变更后强制整屏重绘（面板参数改了要刷一次）
void displayUiRefreshAll();
