# OFM-PowerSupplyDisplay

1.54 英寸 **ST7789（240×240，SPI）** 状态显示屏模块，基于 **LVGL 9.6**，界面全中文。

用于 OpenKNX 的 SC8815 电源设备（本仓库配套 `OFM-SC8815PowerSupply`）：
屏幕上下分屏，上半屏是**总线电源**、下半屏是**辅助电源**，各路显示

| 行 | 内容 | 数据来源 |
|---|---|---|
| 输入电压 | SC8815 ADIN | SC8815 |
| 输入电流 | SC8815 内部 ADC（IBAT） | SC8815 |
| 输出电压 | 输出侧测量 | INA238 |
| 输出电流 | 输出侧测量 | INA238 |
| 温度 | 板载温度传感器 | TMP102 |

SC8815 / INA238 / 温度传感器 / 输入电压出现故障时，屏幕中央弹出红色对话框逐条显示
故障内容，多于 4 条每 3 秒翻页；**故障消失后自动关闭**。

屏幕最左边还有一列 **A/B 输入电源供电状态**：一列向下分成两格（只是排版上与上下半屏对齐），
分别显示 `A`、`B`；正在供电的那一路整格底色变**绿**，未供电为深灰。
引脚（`include/hardware.h`）：

```c
#define OPENKNX_BPS_INPUT_A_POWER_STATUS_PIN        27   // A 路输入电源供电状态
#define OPENKNX_BPS_INPUT_A_POWER_STATUS_ACTIVE_ON  HIGH
#define OPENKNX_BPS_INPUT_B_POWER_STATUS_PIN        28   // B 路输入电源供电状态
#define OPENKNX_BPS_INPUT_B_POWER_STATUS_ACTIVE_ON  HIGH
```

这两根线是**只读输入**，由板上电源检测逻辑驱动；没定义这些宏的板子自动不显示该列。
状态变化时会打一条日志：`Display: input power A on, B off`，
也可用 `disp info` 查看当前状态。

## 开机界面

上电后先显示开机画面（约 2 秒，控制台 `disp boot` 可重播），然后自动进入数值界面：

- 左上角是 **OpenKNX 标志**（`Open` / 绿色总线 / `KNX`，用 LVGL 图元绘制，不需要图片资源），
  右侧是设备名 `SC8815` 与"电源显示"
- 中间列出两路共 **6 颗芯片的初始化状态**：SC8815 / INA238 / TMP102 × 总线 / 辅助。
  每行先显示 `检测中`（灰），再逐行揭开实际结果：`OK`（绿）/ `失败`（红）
- 底部有进度条和总结：`正在检查芯片` → `初始化完成` / `初始化完成，有故障`

开机界面显示期间不刷新数值、不弹报警对话框、也不触发背光超时；
如果一直收不到电源模块的数据，最多 8 秒后也会自动进入数值界面。

启动流程（全部在显示模块内部完成，`main.cpp` 不需要调用任何显示接口）：

1. 背光保持关闭（软开关）初始化面板，ST7789 GRAM 里上一次运行的旧画面不会被照出来
2. 建立并刷出开机界面，**这时才打开背光**
3. 开机界面走完（约 2 秒）后才创建主界面并立即切换过去 —— 主界面对象在此之前根本不存在

## 硬件

引脚全部在 `include/hardware.h` 的 `OPENKNX_BPS_DISPLAY_*` 宏里定义（本模块无硬编码引脚）：

```c
#define OPENKNX_BPS_DISPLAY_SPI          SPI1
#define OPENKNX_BPS_DISPLAY_SCK_PIN      10
#define OPENKNX_BPS_DISPLAY_MOSI_PIN     11
#define OPENKNX_BPS_DISPLAY_CS_PIN       9
#define OPENKNX_BPS_DISPLAY_DC_PIN       8
#define OPENKNX_BPS_DISPLAY_RST_PIN      -1   // -1 = 屏幕 RST 没接 MCU
#define OPENKNX_BPS_DISPLAY_BL_PIN       21   // 背光（PWM 调亮度）
#define OPENKNX_BPS_DISPLAY_SPI_HZ       40000000
#define OPENKNX_BPS_DISPLAY_ROTATION     0
```

**只写了 SPI + 背光，没有触摸**（该尺寸 ST7789 模块通常无触摸），
所以报警对话框靠"故障消失自动关闭"，不需要确认操作。

面板颜色/方向与实物不符时按现象调整（前两项可用控制台现场试，确认后再固化成宏）：

| 现象 | 处理 |
|---|---|
| 整屏像底片（黑变白/反色） | `disp invert on\|off`，或改 `OPENKNX_BPS_DISPLAY_INVERT` |
| 红蓝互换 | 改 `OPENKNX_BPS_DISPLAY_BGR 1` |
| 图像整体偏移 | 改 `OPENKNX_BPS_DISPLAY_GAP_X/Y` |
| 方向不对 | `disp rot 0..3`，或改 `OPENKNX_BPS_DISPLAY_ROTATION` |

## 集成方法

1. `platformio.custom.ini` 里加依赖与 LVGL 配置入口：

   ```ini
   [custom]
   lib_deps =
     lvgl/lvgl@9.6.0
   build_flags =
     -D LV_CONF_INCLUDE_SIMPLE
     -I lib/OFM-PowerSupplyDisplay/src
   ```

2. `src/main.cpp` 注册模块即可（开机界面 / 主界面切换由模块自己管理）：

   ```cpp
   #include "PowerSupplyDisplayModule.h"
   openknx.addModule(3, openknxPowerSupplyDisplayModule);
   ```

3. 电源模块通过**可选依赖**推送数据（`__has_include("PowerSupplyDisplayModule.h")`），
   本模块不反向依赖 SC8815 驱动、也不自己占用 I2C：

   ```cpp
   openknxPowerSupplyDisplayModule.setUnit(false /*bus*/, chipOk, inaOk, tempOk, outputOn,
                                            inputVoltage_mV, inputCurrent_mA,
                                            outputVoltage_mV, outputCurrent_mA, temperatureC, faultCode);
   ```

> 注意：本目录必须是**独立的 git 仓库**（`git init` 过），
> 否则 OGM-Common 的 `scripts/pio/prepare.py` 在生成 `include/versions.h` 时会报
> `TypeError: can only concatenate str (not "bool") to str`。语其它 OFM 模块一致。

## ETS 参数

一个通道 `电源显示屏`，3 个参数：

| 参数 | 说明 |
|---|---|
| 启用显示屏 | 否 = 不初始化 LVGL、背光关闭（硬件没有配屏的机型可以关掉） |
| 背光亮度 | 10..100 %，PWM 占空比 |
| 背光超时 | 常亮 / 10 秒 / 30 秒 / 60 秒 / 5 分钟；熄灭后遇报警、短按任一路 SC8815 复位按键、按 PROG 按键或 `disp on` 立刻点亮 |

## 控制台命令

```
disp                    显示帮助
disp info               屏幕状态 + 两路测量值
disp on | off           开 / 关背光
disp bright <1..100>    背光亮度
disp invert on | off    屏幕反色
disp rot <0..3>         屏幕方向
disp timeout <0..3600>  背光超时秒数（0 = 常亮）
disp test on | off      强制弹出全部 16 条报警文案（检查界面与字库）
disp boot               重播开机界面（OpenKNX 图标 + 芯片初始化状态）
disp redraw             整屏强制重绘
disp reset              面板参数恢复默认
```

## 中文字库

`src/fonts/lv_font_ps_16.c` / `lv_font_ps_20.c` 是**生成物**，
由 `tools/gen-fonts.ps1` 生成（只包含源码字符串里实际出现的汉字 + ASCII + `°`，约 133 个字）。

**改了任何界面文案后必须重新生成**，否则新字会画成方框：

```powershell
powershell -ExecutionPolicy Bypass -File tools\gen-fonts.ps1
```

脚本默认用 `C:\Windows\Fonts\simhei.ttf`，可用 `-FontFile` 换成别的中文字体。

## 注意：app 分区只有 1 MB

RP2040 上留给固件的是 1 MB，LVGL 全开部件会直接链接溢出（实测溢出 48 KB）。
所以 `src/lv_conf.h` 里**关掉了所有用不到的部件**（只留 `OBJ` + `LABEL` + 默认主题，
并关了 flex/grid、simple/mono 主题）。当前占用约 **89 % flash / 45 % RAM**。
要加新部件（表格、图表、按钮…）前请先确认放得下。

## 文件结构

```
src/PowerSupplyDisplayModule.{h,cpp}   OpenKNX 模块：ETS 参数、报警判定、对话框驱动、控制台
src/PowerSupplyDisplayUi.{h,cpp}       LVGL 界面：上下分屏布局、左侧 A/B 输入电源列、数值格式化、报警对话框、开机界面
src/PowerSupplyDisplayPort.{h,cpp}     ST7789 + LVGL 移植：SPI、面板参数、背光、tick
src/lv_conf.h                          LVGL 9.6 配置（含裁剪说明）
src/fonts/                             生成的中文字库
src/PowerSupplyDisplay.share.xml       ETS 参数定义
src/Baggages/Help_cn/                  ETS 上下文帮助（中文）
tools/gen-fonts.ps1                    字库生成脚本
```
