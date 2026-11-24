# 两城之光 · Two-Place Light

一块只关心光的表。

它把两座城市此刻的太阳高度、日出日落、今天还剩多少光，放在同一块 240×320 的屏上。
三颗按键，太阳几何全部在本地算，不联网也能跑——网络只用来对时。

本项目是 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) 的派生应用，
运行在 AI Passport 那块 ESP32-C3 可穿戴板子上。

## 三个页面

| 页面 | 内容 |
| --- | --- |
| **ARC** | 全天太阳高度画成两条曲线（城市 1 蓝、城市 2 橙）+ 地平线 + 一根竖直的"现在"标记 |
| **PAIR** | 两城数字并排：此刻高度、日出、日落，以及哪边更亮 |
| **LEFT** | 距离下一次日落/日出还有多久、民用晨昏时刻、明天比今天多或少多少光 |

## 按键

| 操作 | 作用 |
| --- | --- |
| `上` / `下` | 上一页 / 下一页 |
| `确定` 短按 | 背光 100% → 60% → 30% 循环 |
| `确定` 长按 | 息屏；任意键唤醒 |
| `下` 长按 | 打开设置热点 |

## 设置两座城市

出厂默认是威海（37.51 N, 122.12 E）和天津（39.08 N, 117.20 E）。要换成自己关心的两个地方：

1. 设备上长按 `下`，屏幕显示 `SETTINGS`；
2. 手机连热点 `TwoPlaceLight`，密码 `lingan-is-so-cute`；
3. 浏览器打开 `http://192.168.4.1`，填两座城市的名字、纬度、经度、UTC 偏移；
4. 保存后设备自动重启，首页立刻按新坐标画曲线。

两条约束：

- **名字只能是 ASCII**（字母、数字、空格、`-`、`_`，最长 12 字符）。屏上没有中文字库，
  “北京”要写成 `BEIJING`。
- **两座城市共用一个 UTC 偏移**（默认 `480`，即 UTC+8），必须是 15 的整数倍。跨时区的两城
  暂不支持。

设置页同时可以改 Wi-Fi：名字留空表示沿用现有网络。

## 时间从哪来

板子没有独立 RTC 芯片，时间靠 ESP32-C3 的 RTC 经 newlib 维持。按这个顺序取时间：

1. **Wi-Fi + SNTP**：连路由器，问 `ntp.aliyun.com`；
2. **设置热点里手填**；
3. **三键手动对时**：抢时间阶段长按 `确定` 跳过联网。

RTC 走的是片内 RC 振荡器，会漂，所以固件在有凭证时**开机必校一次**；之后**每 6 小时在后台
静默校时**，失败就继续用旧时间，不会把界面卡在联网页上。

## 构建

需要 ESP-IDF **5.5.x**（开发者本机用 5.5.5，CI 用 5.5.3）。

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash monitor
```

合成单文件（玩法中心要的是 `esp-merged-0x0`）：

```bash
idf.py merge-bin
```

仓库自带两道门禁：

```bash
./tools/validate.sh --static    # 仓库约定检查 + 宿主测试
./tools/validate.sh --firmware  # 构建 + 合并镜像校验
```

`--static` 里的宿主测试需要一台有 `cc` 的机器（CI 的 Ubuntu 上有）；Windows 上只能跑语法检查。

## 文档

- 应用说明：[docs/projects/two-place-light.md](docs/projects/two-place-light.md)（中文版 `two-place-light.zh_CN.md`）
- 开发约定：[AGENTS.md](AGENTS.md)、[docs/development/ai-guide.md](docs/development/ai-guide.md)
- 硬件契约：`components/bsp/include/bsp_pins.h`

## 已知限制

- **息屏不是休眠**：长按 `确定` 只关背光与屏幕，CPU 与 Wi-Fi 仍在跑，不省电。
- 背光档位不跨重启保存。
- 界面只有英文（没有中文字库，省 flash）。
- 没有网络时只剩 RC 推出来的 RTC，两次手动对时之间的误差没有上限。

## 谁做的

这个项目由一个人和一个 AI 助手一起做完：

- **阿渊（GitHub: [Innovant-cloud](https://github.com/Innovant-cloud)）**：硬件、真机验证、
  每一次“要不要这样”的决定，以及把仓库和发布这些事情扛在手里。
- **翎岸 / 小岸（AI 助手）**：实现、测试、文档，以及把每一次失败查到根因——
  包括那些只有在 CI 上才会暴露的错。

代码不是一个人写的，也不是纯粹机器生成的。署名用 `Co-authored-by` 记在提交里，
可以逐条追溯。

## 许可证与来源声明

本项目基于 **FoloToy AI Passport** 派生，原始项目以 **MIT License** 发布，
Copyright (c) 2026 FoloToy。

**原始版权声明与许可全文原样保留在 [LICENSE](LICENSE)，未作任何修改。**
本仓库的修改部分版权归本仓库作者所有。`components/`、`docs/` 中的开发约定与 BSP 代码
同样来自上游，沿用同一份 MIT 许可。
