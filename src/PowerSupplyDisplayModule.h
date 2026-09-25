#pragma once

#include "OpenKNX.h"
#include "hardware.h"
#include "knxprod.h"

#include "PowerSupplyDisplayUi.h"

/* ============================================================================
 *  ETS 模块 "电源显示" (PowerSupplyDisplay)
 * ----------------------------------------------------------------------------
 *  1.54" ST7789 (240x240, SPI) 状态屏，LVGL 9.6，全中文界面：
 *
 *    上半屏 = 总线电源，下半屏 = 辅助电源，每半屏 5 行：
 *        输入电压 / 输入电流   (SC8815 ADIN + IBAT)
 *        输出电压 / 输出电流   (输出侧 INA238)
 *        温度                 (TMP102)
 *
 *    SC8815 / INA238 / 温度传感器 / 输入电压 出现故障时，屏幕弹出红色对话框
 *    逐条显示故障内容；故障消失后自动关闭。
 *
 *  数据来源：由 OFM-SC8815PowerSupply 通过 setUnit() 推送（与 OFM-BuzzerAlert 的
 *  setFaults() 同样的"可选依赖"模式），本模块不反向依赖 SC8815 驱动，
 *  也不自己碰 I2C 总线。
 *
 *  硬件引脚见 include/hardware.h 的 OPENKNX_BPS_DISPLAY_* 宏。
 * ========================================================================== */

/// 供别的模块用 __has_include 判断本模块是否存在
#define OPENKNX_POWER_SUPPLY_DISPLAY_AVAILABLE 1

/// 背光被超时熄灭后，最多监听几个用来"叫醒"屏幕的按键
#define DISPLAY_WAKE_BUTTON_MAX 4

/// 供电侧编号（顺序与 setUnit() 的参数、界面上下半屏一致）
enum PowerSupplyDisplaySide : uint8_t
{
    PowerSupplyDisplay_Bus = 0,
    PowerSupplyDisplay_Aux = 1,
    PowerSupplyDisplay_SideCount = 2,
};

/**
 * @brief 故障位定义
 *
 * 必须与 OFM-SC8815PowerSupply 的 BpsFault 一致（这里不包含它的头文件，
 * 免得两个模块互相拽依赖）。每一位对应一条对话框里的一行。
 */
enum PowerSupplyDisplayFault : uint8_t
{
    DisplayFault_SC8815NoAnswer = 0x01,
    DisplayFault_INANoAnswer = 0x02,
    DisplayFault_TempNoAnswer = 0x04,
    DisplayFault_VbusShort = 0x08,
    DisplayFault_OTP = 0x10,
    DisplayFault_OverCurrent = 0x20,
    DisplayFault_InputHigh = 0x40,
    DisplayFault_InputLow = 0x80,
};

class PowerSupplyDisplayModule : public OpenKNX::Module
{
  public:
    const std::string name() override;
    const std::string version() override;

    void setup(bool configured) override;
    void loop(bool configured) override;

    bool processCommand(const std::string cmd, bool diagnoseKo) override;
    void showHelp() override;
    void showInformations() override;

    // ---- 对外 API：给电源模块推送实时数据 ----
    /// 推送一路的全部显示数据（每轮 loop 调一次即可）
    void setUnit(bool isAux, bool chipOk, bool inaOk, bool tempOk, bool outputOn,
                 float inputVoltage_mV, float inputCurrent_mA,
                 float outputVoltage_mV, float outputCurrent_mA, float temperatureC,
                 uint8_t faultCode);

    /// 显示是否可用（初始化失败时为 false，控制台仍可查询）
    bool available() const { return _displayOk; }

  private:
    PowerSupplyDisplayValues _unit[PowerSupplyDisplay_SideCount];

    bool _displayOk = false;
    bool _enabled = true;     // ETS：显示总开关
    uint8_t _brightness = 100; // ETS：背光亮度 %
    uint16_t _timeoutS = 0;   // ETS：背光超时（0 = 常亮）
    bool _backlightOn = true;
    uint32_t _lastActivity = 0;

    uint32_t _lvglTimer = 0;
    uint32_t _uiTimer = 0;

    uint16_t _alarmShownMask = 0; // 上一次已显示的报警集合（用于判断"有变化"）
    uint8_t _alarmItemSide[16] = {0}; // 当前活跃报警项：所属侧
    uint8_t _alarmItemBit[16] = {0};  // 当前活跃报警项：故障位号 0..7
    uint8_t _alarmItemCount = 0;
    uint8_t _alarmPage = 0;       // 对话框当前页（每页 DISPLAY_ALARM_PAGE_SIZE 项）
    uint32_t _alarmCycleTimer = 0;
    bool _alarmDirty = false;      // 强制下一次重新生成对话框（如 disp test 切换）
    bool _testMode = false;       // 控制台 disp test：无论真实故障都弹窗

    // ---- 背光唤醒按键（按下 = 有效电平，全部由别的模块/核心初始化）----
    // 本模块只读这些引脚，不 pinMode、不消费事件：
    //   PROG 按键      -> OpenKNX 核心（编程模式）
    //   两路复位按键   -> OFM-SC8815PowerSupply（长按 3 s 才复位）
    uint8_t _wakePin[DISPLAY_WAKE_BUTTON_MAX] = {0};
    uint8_t _wakeActive[DISPLAY_WAKE_BUTTON_MAX] = {0};
    bool _wakePressed[DISPLAY_WAKE_BUTTON_MAX] = {false};
    uint32_t _wakeTimer[DISPLAY_WAKE_BUTTON_MAX] = {0};
    uint8_t _wakeCount = 0;

    void applyParameters(bool configured);
    bool anyAlarm() const;
    void showAlarmPage();
    void updateAlarmDialog(uint32_t now);
    void setBacklight(bool on);
    void wake();
    void updateBacklight(uint32_t now);
    void registerWakeButtons();
    void addWakeButton(uint8_t pin, uint8_t activeLevel);
    bool anyWakeButtonPressed(uint32_t now);
};

extern PowerSupplyDisplayModule openknxPowerSupplyDisplayModule;
