#pragma once

#include <lvgl.h>
#include <stdint.h>

/* ============================================================================
 *  240x240 界面：上下各半屏 + 报警对话框
 * ----------------------------------------------------------------------------
 *  上半屏 = 总线电源，下半屏 = 辅助电源，每半屏显示：
 *      输入电压 / 输入电流   (SC8815 ADIN + IBAT)
 *      输出电压 / 输出电流   (输出侧 INA238)
 *      温度                 (TMP102)
 *  报警时在 lv_layer_top() 上弹出一个红色对话框（不改变底层数值显示）。
 *
 *  本文件只做界面，不碰硬件、不读参数：数据由 PowerSupplyDisplayModule 推进来。
 * ========================================================================== */

/// 一侧（总线/辅助）要显示的数值
struct PowerSupplyDisplayValues
{
    bool valid = false;             ///< 是否已收到过数据
    bool outputOn = false;          ///< 输出是否接通（PGATE）
    uint8_t fault = 0;              ///< 故障位（OpenKNX BpsFault 的位定义）
    float inputVoltage_mV = 0.0f;   ///< SC8815 ADIN 换算后的输入电压
    float inputCurrent_mA = 0.0f;   ///< SC8815 IBAT
    float outputVoltage_mV = 0.0f;  ///< INA238 输出电压
    float outputCurrent_mA = 0.0f;  ///< INA238 输出电流
    float temperatureC = 0.0f;      ///< TMP102 温度
};

/// 建立静态界面（只调用一次，必须在 displayPortBegin() 之后）
void displayUiBegin();

/// 更新一侧的数值显示（内部做了"值没变就不碰 LVGL"的判断）
void displayUiSetSide(bool isAux, const PowerSupplyDisplayValues &values);

/// 报警对话框：show=true 时弹出/刷新内容
/// @param body 多行文本（\n 分隔），nullptr 或空串表示只显示标题
void displayUiSetAlarm(bool show, const char *title, const char *body);

/// 报警对话框当前是否可见
bool displayUiAlarmVisible();

/// 颜色反转/方向变更后强制整屏重绘（面板参数改了要刷一次）
void displayUiRefreshAll();
