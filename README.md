# Xiaomi Pad 5 鼠标滚轮方向反转 (evdev / uinput)

在 **Xiaomi Pad 5 系列**上反转鼠标滚轮方向：**本机应用和 UU远程（UU Remote）远程会话都适用**。反转后浏览器滚动方向与 PC 一致。

系统级方案：抓取物理鼠标的 evdev 事件，通过 `/dev/uinput` 重新发一个虚拟鼠标。**不依赖 Frida，不需要改主机，开机自启。**

## 支持范围

- Xiaomi Pad 5 系列（实测 Xiaomi Pad 5 Pro 12.4，型号 `22081281AC`，Android 12 / MIUI）
- 其余 Xiaomi Pad 5 型号未测；其他 Android 设备也未测
- 需要 root（KernelSU / SukiSU；Magisk 理论上可用）和内核 `CONFIG_INPUT_UINPUT`

## 背景

最初是为 UU远程 做的；后续实测发现这台平板的**本机应用滚轮方向也是反的**，浏览器上下滚动与 PC 相反。启用本模块后，本机应用和 UU远程 两个场景都正常，浏览器浏览体验与电脑一致。

v1.3 起虚拟鼠标在物理鼠标重连时保持存在，避免 UU远程 绑定到已销毁的旧设备；v1.4 起默认 `invert=0`；v1.5 起目标鼠标名可配置。

Frida 钩 `MotionEvent.getAxisValue(AXIS_VSCROLL)` 取反也能达到反转效果，但需要主机 watcher，重启后容易失效。本项目把反转下沉到 Android 输入层。

## 原理

```
物理鼠标 (BT5.0 Mouse / Xiaomi Mouse)
        │
        │  EVIOCGRAB（独占）
        ▼
   uu_wheel_flip
        │
        │  write /dev/uinput
        ▼
虚拟鼠标 (UU Wheel Flip ...)
        │
        ▼
Android InputReader → UU远程 / 本地 App
```

关键点：**最终验证配置是 `invert=0` 直通**。v1.3 的 daemon 只在物理鼠标断开时关闭物理 fd，**不销毁 `/dev/uinput` 虚拟鼠标**；物理鼠标重连后复用同一个虚拟设备。这样可以避免开机后 BT 鼠标重连导致 UU远程 仍绑定旧设备、滚轮方向再次反转。v1.4 起模块默认 `invert=0`。

调试过程中曾尝试 `invert=1`，但方向会受虚拟设备是否重建影响，不作为默认配置。

## 环境

- Android arm64-v8a
- 已 root（KernelSU / SukiSU；Magisk 理论上也可用）
- 内核开启 `CONFIG_INPUT_UINPUT`，存在 `/dev/uinput`
- 测试环境：Xiaomi Pad 5 Pro 12.4（`22081281AC`），Android 12 / SDK 31，SukiSU Ultra 3.2.0

## 构建

需要 [Zig](https://ziglang.org/) 0.13.x（交叉编译）和 `zip`：

```bash
ZIG=/path/to/zig ./build.sh
```

生成：

- `module/uu_wheel_flip`：ARM64 静态二进制
- `dist/xiaomi-pad5-mouse-wheel-invert.zip`：KernelSU/Magisk 模块包

手动编译：

```bash
zig cc -target aarch64-linux-musl -static -O2 -o uu_wheel_flip src/uu_wheel_flip.c
```

## 安装

```bash
adb push dist/xiaomi-pad5-mouse-wheel-invert.zip /data/local/tmp/
adb shell "su -c 'ksud module install /data/local/tmp/xiaomi-pad5-mouse-wheel-invert.zip'"
adb reboot
```

Magisk：

```bash
adb shell "su -c 'magisk --install-module /data/local/tmp/xiaomi-pad5-mouse-wheel-invert.zip'"
adb reboot
```

开机后模块会：

1. 等待约 15 秒
2. 启动 `uu_wheel_flip`
3. 抓取名字包含 `BT5.0 Mouse` / `Xiaomi Mouse` 的鼠标
4. 创建 `UU Wheel Flip ...` 虚拟鼠标

## 配置

控制文件：

```bash
adb shell "su -c 'echo 0 > /data/local/tmp/uu_wheel_flip.invert'"
```

- `0`：直通，不翻转（v1.4+ 默认，当前验证正确）
- `1`：翻转 `REL_WHEEL` / `REL_HWHEEL`

修改后**立即生效**，不需要重启 daemon。

目标鼠标名：

```bash
adb shell "su -c 'cat /data/local/tmp/uu_wheel_flip.targets'"
```

默认内容：

```text
BT5.0 Mouse
Xiaomi Mouse
```

- 每行一个名字，按**子串**匹配；支持 `*` / `?` 通配符
- `#` 开头是注释，空行忽略
- 文件不存在或没有有效行时，使用内置的 `BT5.0 Mouse` / `Xiaomi Mouse`
- 新增名字后，下次扫描（约 2 秒）生效；替换已连接的鼠标名时，重连鼠标或重启 daemon
- 手动运行时也可以用参数覆盖文件：`uu_wheel_flip "Mouse Name"`

日志：

```bash
adb shell "su -c 'cat /data/local/tmp/uu_wheel_flip.log'"
```

## 注意

- 换鼠标不需要重编译：把 `/proc/bus/input/devices` 里的 `N: Name=` 加到 `/data/local/tmp/uu_wheel_flip.targets` 即可。v1.5 起支持。
- `service.sh` 只在控制文件不存在时写入默认值。从旧版升级且已有控制文件时不会被覆盖；如果之前手动设过 `1`，升级后请执行上面的 `echo 0`。
- 开机后 daemon 有约 15 秒启动延迟；如果刚开机就打开 UU远程，可能先绑到物理鼠标，重开一次 App 即可。
- BT 鼠标在开机后可能重连一次；v1.3 会保留虚拟鼠标并复用，不需要因为重连重开 UU远程。
- **不要同时运行主机端的 `WheelFlip.exe`**。它会把 UU远程 注入的滚轮再翻一次，和本方案形成双重取反。本项目测试时已将其禁用。
- 如果之前装过 Frida 的 `uu-wheel-fix` 模块，请先卸载，避免残留的 frida-server 干扰。
- 不同 ROM/输入栈可能有差异；如果方向不对，先尝试切换 `invert`，再检查是否有其他软件在改滚轮方向。

## License

MIT
