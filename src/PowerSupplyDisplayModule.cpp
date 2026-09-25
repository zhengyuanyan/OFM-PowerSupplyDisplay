#include "PowerSupplyDisplayModule.h"

#include "ModuleVersionCheck.h"
#include "PowerSupplyDisplayPort.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PowerSupplyDisplayModule openknxPowerSupplyDisplayModule;

/* ============================================================================
 *  常量
 * ========================================================================== */

/// 报警对话框每页显示几行
#define DISPLAY_ALARM_PAGE_SIZE 4
/// 多于一行报警时的翻页间隔
#define DISPLAY_ALARM_PAGE_MS 3000

/// 数值刷新周期（数值变化很慢，4 Hz 足够，省 CPU 给 KNX 栈）
#define DISPLAY_VALUE_REFRESH_MS 250

/// LVGL 心跳周期
#define DISPLAY_LVGL_TICK_MS 5

/// 每个故障位的显示文案（顺序 = 故障位 bit0..bit7）
static const char *const cAlarmText[8] = {
    "SC8815 无应答", // 0x01
    "INA238 无应答", // 0x02
    "温度传感器无应答", // 0x04
    "输出短路",       // 0x08
    "SC8815 过温",   // 0x10
    "INA238 过流",   // 0x20
    "输入电压过高",   // 0x40
    "输入电压过低",   // 0x80
};

static const char *const cSideAlarmTitle[PowerSupplyDisplay_SideCount] = {"总线报警", "辅助报警"};

/// ETS“背光超时”的取值表（参数里存的是索引）
static const uint16_t cTimeoutSeconds[] = {0, 10, 30, 60, 300};
static const uint8_t cTimeoutCount = sizeof(cTimeoutSeconds) / sizeof(cTimeoutSeconds[0]);

/* ============================================================================
 *  模块基本信息
 * ========================================================================== */

const std::string PowerSupplyDisplayModule::name()
{
    return "PowerSupplyDisplay";
}

const std::string PowerSupplyDisplayModule::version()
{
    return MODULE_PowerSupplyDisplay_Version;
}

/* ============================================================================
 *  参数
 * ========================================================================== */

void PowerSupplyDisplayModule::applyParameters(bool configured)
{
    if (!configured)
    {
        // 没下载过 ETS 参数：按最保守的方式开屏（全亮、常亮）
        _enabled = true;
        _brightness = 100;
        _timeoutS = 0;
        return;
    }

    _enabled = ParamDSP_Enabled;

    _brightness = (uint8_t)ParamDSP_Brightness;
    if (_brightness < 1)
        _brightness = 1; // 要关屏请用"背光超时"，亮度 0 会让人以为屏坏了
    if (_brightness > 100)
        _brightness = 100;

    uint8_t timeoutIndex = (uint8_t)ParamDSP_BacklightTimeout;
    if (timeoutIndex >= cTimeoutCount)
        timeoutIndex = 0;
    _timeoutS = cTimeoutSeconds[timeoutIndex];
}

/* ============================================================================
 *  生命周期
 * ========================================================================== */

void PowerSupplyDisplayModule::setup(bool configured)
{
    applyParameters(configured);

    _displayOk = displayPortBegin();
    if (!_displayOk)
    {
        logErrorP("Display: ST7789 init failed - display disabled");
        return;
    }

    displayUiBegin();
    registerWakeButtons();

    _backlightOn = true;
    displayPortSetBacklight(_enabled ? _brightness : 0);
    _lastActivity = delayTimerInit();

    logInfoP("Display: 240x240 ST7789 ready, %s, brightness %u%%, backlight timeout %us",
             _enabled ? "enabled" : "disabled", _brightness, _timeoutS);
}

void PowerSupplyDisplayModule::loop(bool configured)
{
    if (!_displayOk)
        return;

    const uint32_t now = delayTimerInit();

    if (!_enabled)
    {
        // ETS 里关掉了显示：不跑 LVGL，背光也关掉
        setBacklight(false);
        return;
    }

    // LVGL 心跳（只有真的有东西要重绘时才干活）
    if (delayCheck(_lvglTimer, DISPLAY_LVGL_TICK_MS))
    {
        _lvglTimer = now;
        lv_timer_handler();
    }

    // 数值刷新
    if (delayCheck(_uiTimer, DISPLAY_VALUE_REFRESH_MS))
    {
        _uiTimer = now;
        displayUiSetSide(false, _unit[PowerSupplyDisplay_Bus]);
        displayUiSetSide(true, _unit[PowerSupplyDisplay_Aux]);
    }

    updateAlarmDialog(now);
    updateBacklight(now);
}

/* ============================================================================
 *  数据入口
 * ========================================================================== */

void PowerSupplyDisplayModule::setUnit(bool isAux, bool chipOk, bool inaOk, bool tempOk, bool outputOn,
                                       float inputVoltage_mV, float inputCurrent_mA,
                                       float outputVoltage_mV, float outputCurrent_mA, float temperatureC,
                                       uint8_t faultCode)
{
    PowerSupplyDisplayValues &v = _unit[isAux ? PowerSupplyDisplay_Aux : PowerSupplyDisplay_Bus];

    v.valid = chipOk || inaOk || tempOk;
    v.outputOn = outputOn;
    v.fault = faultCode;
    v.inputVoltage_mV = inputVoltage_mV;
    v.inputCurrent_mA = inputCurrent_mA;
    v.outputVoltage_mV = outputVoltage_mV;
    v.outputCurrent_mA = outputCurrent_mA;
    v.temperatureC = temperatureC;
}

/* ============================================================================
 *  报警对话框
 * ========================================================================== */

bool PowerSupplyDisplayModule::anyAlarm() const
{
    if (_testMode)
        return true;
    for (uint8_t s = 0; s < PowerSupplyDisplay_SideCount; s++)
        if (_unit[s].fault != 0)
            return true;
    return false;
}

void PowerSupplyDisplayModule::showAlarmPage()
{
    if (_alarmItemCount == 0)
    {
        displayUiSetAlarm(false, nullptr, nullptr);
        return;
    }

    const uint8_t first = (uint8_t)(_alarmPage * DISPLAY_ALARM_PAGE_SIZE);
    const uint8_t last = (uint8_t)(first + DISPLAY_ALARM_PAGE_SIZE);
    const uint8_t end = (last < _alarmItemCount) ? last : _alarmItemCount;

    // 标题：整页同侧就用该侧的名字，混排就用通用的
    const uint8_t side = _alarmItemSide[first];
    bool sameSide = true;
    for (uint8_t i = (uint8_t)(first + 1); i < end; i++)
    {
        if (_alarmItemSide[i] != side)
        {
            sameSide = false;
            break;
        }
    }

    char body[256];
    body[0] = '\0';
    for (uint8_t i = first; i < end; i++)
    {
        if (i > first)
            strncat(body, "\n", sizeof(body) - strlen(body) - 1);
        strncat(body, cAlarmText[_alarmItemBit[i]], sizeof(body) - strlen(body) - 1);
    }

    // 还有下一页时给个提示，让人知道屏幕在轮播
    if (_alarmItemCount > DISPLAY_ALARM_PAGE_SIZE)
    {
        char more[32];
        snprintf(more, sizeof(more), "\n(%u/%u)", (unsigned)(_alarmPage + 1),
                 (unsigned)((_alarmItemCount + DISPLAY_ALARM_PAGE_SIZE - 1) / DISPLAY_ALARM_PAGE_SIZE));
        strncat(body, more, sizeof(body) - strlen(body) - 1);
    }

    displayUiSetAlarm(true, sameSide ? cSideAlarmTitle[side] : "电源报警", body);
}

void PowerSupplyDisplayModule::updateAlarmDialog(uint32_t now)
{
    uint16_t mask = 0;
    for (uint8_t s = 0; s < PowerSupplyDisplay_SideCount; s++)
        mask |= (uint16_t)_unit[s].fault << (s * 8);
    if (_testMode)
        mask = 0xFFFF;

    bool refresh = false;

    // 注意：不能只依赖 mask != _alarmShownMask，比如 disp test 关闭后
    // 真实掩码可能正好等于上一次的值，必须用 _alarmDirty 显式要求重建。
    if (mask != _alarmShownMask || _alarmDirty)
    {
        _alarmDirty = false;
        _alarmShownMask = mask;
        _alarmPage = 0;
        _alarmCycleTimer = now;

        _alarmItemCount = 0;
        for (uint8_t s = 0; s < PowerSupplyDisplay_SideCount; s++)
        {
            for (uint8_t b = 0; b < 8; b++)
            {
                if (mask & ((uint16_t)1 << (s * 8 + b)))
                {
                    _alarmItemSide[_alarmItemCount] = s;
                    _alarmItemBit[_alarmItemCount] = b;
                    _alarmItemCount++;
                }
            }
        }

        if (_alarmItemCount > 0)
        {
            logInfoP("Display: %u alarm(s) active (mask 0x%04X)", _alarmItemCount, mask);
            wake();
        }
        else
        {
            logInfoP("Display: no active alarm");
        }
        refresh = true;
    }
    else if (_alarmItemCount > DISPLAY_ALARM_PAGE_SIZE && delayCheck(_alarmCycleTimer, DISPLAY_ALARM_PAGE_MS))
    {
        _alarmCycleTimer = now;
        const uint8_t pages = (uint8_t)((_alarmItemCount + DISPLAY_ALARM_PAGE_SIZE - 1) / DISPLAY_ALARM_PAGE_SIZE);
        _alarmPage = (uint8_t)((_alarmPage + 1) % pages);
        refresh = true;
    }

    if (refresh)
        showAlarmPage();
}

/* ============================================================================
 *  背光
 * ========================================================================== */

void PowerSupplyDisplayModule::setBacklight(bool on)
{
    if (on == _backlightOn)
        return;
    _backlightOn = on;
    displayPortSetBacklight(on ? _brightness : 0);
}

void PowerSupplyDisplayModule::wake()
{
    _lastActivity = delayTimerInit();
    setBacklight(true);
}

void PowerSupplyDisplayModule::updateBacklight(uint32_t now)
{
    // 常亮，或者有报警（报警时一定要能看见）
    if (_timeoutS == 0 || anyAlarm())
    {
        wake();
        return;
    }

    // 按键唤醒：PROG 按键 + 两路 SC8815 的外部复位按键（短按即可点亮）。
    // 这里只读电平，长按 3 s 的复位逻辑完全由电源模块自己处理。
    if (anyWakeButtonPressed(now))
    {
        wake();
        return;
    }

    if (_backlightOn && delayCheck(_lastActivity, (uint32_t)_timeoutS * 1000u))
        setBacklight(false);
}

void PowerSupplyDisplayModule::registerWakeButtons()
{
    // PROG 按键：OpenKNX 核心已把它配成 INPUT_PULLUP（按下 = LOW）
#ifdef PROG_BUTTON_PIN
    addWakeButton(PROG_BUTTON_PIN, LOW);
#endif

    // 两路 SC8815 的外部复位按键：有效电平跟随电源模块的 *_ACTIVE_ON 宏。
    // 读这两根线不会影响“长按 3 s 才触发复位”的判定。
#if defined(OPENKNX_BPS_SC8815_BUS_POWER_SUPPLY_RESET_PIN) && defined(OPENKNX_BPS_SC8815_BUS_POWER_SUPPLY_RESET_PIN_ACTIVE_ON)
    addWakeButton(OPENKNX_BPS_SC8815_BUS_POWER_SUPPLY_RESET_PIN,
                  OPENKNX_BPS_SC8815_BUS_POWER_SUPPLY_RESET_PIN_ACTIVE_ON);
#endif
#if defined(OPENKNX_BPS_SC8815_AUX_POWER_SUPPLY_RESET_PIN) && defined(OPENKNX_BPS_SC8815_AUX_POWER_SUPPLY_RESET_PIN_ACTIVE_ON)
    addWakeButton(OPENKNX_BPS_SC8815_AUX_POWER_SUPPLY_RESET_PIN,
                  OPENKNX_BPS_SC8815_AUX_POWER_SUPPLY_RESET_PIN_ACTIVE_ON);
#endif
}

void PowerSupplyDisplayModule::addWakeButton(uint8_t pin, uint8_t activeLevel)
{
    if (_wakeCount >= DISPLAY_WAKE_BUTTON_MAX)
        return;

    _wakePin[_wakeCount] = pin;
    _wakeActive[_wakeCount] = activeLevel;
    _wakePressed[_wakeCount] = false;
    _wakeTimer[_wakeCount] = 0;
    _wakeCount++;
}

/**
 * @brief 扫描唤醒按键，返回是否出现一次新的“按下”
 *
 * 带去抖，只在按下的那个边沿返回 true（一直按住不会反复唤醒）。
 * 纯旁听：不 pinMode、不消费事件，所以既不影响 OpenKNX 的编程模式，
 * 也不影响电源模块的长按复位判定。
 */
bool PowerSupplyDisplayModule::anyWakeButtonPressed(uint32_t now)
{
    bool pressed = false;

    for (uint8_t i = 0; i < _wakeCount; i++)
    {
        const uint8_t level = openknx.gpio.digitalRead(_wakePin[i]) ? HIGH : LOW;
        const bool raw = (level == _wakeActive[i]);
        if (raw == _wakePressed[i])
            continue;

        // 电平变化：等去抖时间过去再确认
        if (!delayCheckMillis(_wakeTimer[i], OPENKNX_BUTTON_DEBOUNCE))
            continue;

        _wakeTimer[i] = now;
        _wakePressed[i] = raw;
        if (raw)
            pressed = true;
    }

    return pressed;
}

/* ============================================================================
 *  控制台
 * ========================================================================== */

void PowerSupplyDisplayModule::showInformations()
{
    logInfoP("Display: %s, %s, brightness %u%%, invert %u, rotation %u, timeout %us",
             _displayOk ? "port ok" : "port FAILED", _enabled ? "enabled" : "disabled", _brightness,
             displayPortInvert() ? 1 : 0, displayPortRotation(), _timeoutS);
    logInfoP("Display: backlight %s, %u alarm item(s), mask 0x%04X, test mode %s",
             _backlightOn ? "on" : "off", _alarmItemCount, _alarmShownMask, _testMode ? "on" : "off");
    logInfoP("Display: %u wake button(s) armed (PROG + SC8815 reset pins)", _wakeCount);

    static const char *const sideNames[PowerSupplyDisplay_SideCount] = {"总线", "辅助"};
    for (uint8_t s = 0; s < PowerSupplyDisplay_SideCount; s++)
    {
        const PowerSupplyDisplayValues &v = _unit[s];
        logInfoP("Display %s: %s, in %.1f V / %.0f mA, out %.1f V / %.0f mA, %.1f C, fault 0x%02X",
                 sideNames[s], v.valid ? (v.outputOn ? "on" : "off") : "no data", (double)(v.inputVoltage_mV / 1000.0f),
                 (double)v.inputCurrent_mA, (double)(v.outputVoltage_mV / 1000.0f), (double)v.outputCurrent_mA,
                 (double)v.temperatureC, v.fault);
    }
}

void PowerSupplyDisplayModule::showHelp()
{
    openknx.console.printHelpLine("disp", "显示电源显示模块的帮助");
    openknx.console.printHelpLine("disp info", "显示屏幕状态与两路测量值");
    openknx.console.printHelpLine("disp on|off", "开 / 关背光（不带参数则切换）");
    openknx.console.printHelpLine("disp bright <1..100>", "设置背光亮度（%）");
    openknx.console.printHelpLine("disp invert on|off", "屏幕反色开关（画面像底片时切换）");
    openknx.console.printHelpLine("disp rot <0..3>", "屏幕方向 0/90/180/270 度");
    openknx.console.printHelpLine("disp timeout <0..300>", "背光超时秒数（0 = 常亮）");
    openknx.console.printHelpLine("disp test on|off", "强制弹出所有报警对话框（检查界面/字库）");
    openknx.console.printHelpLine("disp redraw", "整屏强制重绘");
    openknx.console.printHelpLine("(硬件)", "背光被超时熄灭后：短按 PROG 或任一路复位按键即可点亮");
}

bool PowerSupplyDisplayModule::processCommand(const std::string cmd, bool diagnoseKo)
{
    if (cmd.compare(0, 4, "disp") != 0)
        return false;

    if (cmd.length() == 4)
    {
        showHelp();
        return true;
    }

    const std::string arguments = cmd.substr(5);

    if (arguments == "info" || arguments == "status")
    {
        showInformations();
        return true;
    }

    if (arguments.rfind("on", 0) == 0 && arguments.length() == 2)
    {
        wake();
        logInfoP("Display: backlight on");
        return true;
    }

    if (arguments.rfind("off", 0) == 0 && arguments.length() == 3)
    {
        _backlightOn = true; // 让 setBacklight 真的写一次
        setBacklight(false);
        logInfoP("Display: backlight off");
        return true;
    }

    if (arguments.rfind("bright ", 0) == 0)
    {
        const int value = atoi(arguments.c_str() + 7);
        if (value >= 1 && value <= 100)
        {
            _brightness = (uint8_t)value;
            _backlightOn = false; // 强制重写
            setBacklight(true);
            logInfoP("Display: brightness %u%%", _brightness);
        }
        else
        {
            logInfoP("disp: brightness out of range (1..100)");
        }
        return true;
    }

    if (arguments.rfind("invert ", 0) == 0)
    {
        displayPortSetInvert(arguments.find("off") == std::string::npos);
        displayUiRefreshAll();
        logInfoP("Display: invert %s", displayPortInvert() ? "on" : "off");
        return true;
    }

    if (arguments.rfind("rot ", 0) == 0)
    {
        const int value = atoi(arguments.c_str() + 4);
        if (value >= 0 && value <= 3)
        {
            displayPortSetRotation((uint8_t)value);
            displayUiRefreshAll();
            logInfoP("Display: rotation %u", displayPortRotation());
        }
        else
        {
            logInfoP("disp: rotation out of range (0..3)");
        }
        return true;
    }

    if (arguments.rfind("timeout ", 0) == 0)
    {
        const int value = atoi(arguments.c_str() + 8);
        if (value >= 0 && value <= 3600)
        {
            _timeoutS = (uint16_t)value;
            wake();
            logInfoP("Display: backlight timeout %us", _timeoutS);
        }
        else
        {
            logInfoP("disp: timeout out of range (0..3600)");
        }
        return true;
    }

    if (arguments.rfind("test", 0) == 0)
    {
        _testMode = !(arguments.find("off") != std::string::npos);
        _alarmDirty = true; // 强制重新生成对话框（开/关都要立即生效）
        wake();
        logInfoP("Display: test mode %s", _testMode ? "on" : "off");
        return true;
    }

    if (arguments == "redraw")
    {
        displayUiRefreshAll();
        logInfoP("Display: redraw");
        return true;
    }

    if (arguments == "reset")
    {
        displayPortSetInvert(true);
        displayPortSetRotation(OPENKNX_BPS_DISPLAY_ROTATION);
        displayUiRefreshAll();
        logInfoP("Display: panel settings reset to defaults");
        return true;
    }

    logInfoP("disp: bad arguments");
    if (diagnoseKo)
        openknx.console.writeDiagenoseKo("disp: bad arguments");

    showHelp();
    return true;
}
