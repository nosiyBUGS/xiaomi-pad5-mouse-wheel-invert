# UU Remote Wheel Fix (evdev / uinput)

在 Android 平板上修复 **UU远程（UU Remote）** 远程会话鼠标滚轮方向反转的问题。

系统级方案：抓取物理鼠标的 evdev 事件，通过 `/dev/uinput` 重新发一个虚拟鼠标。**不依赖 Frida，不需要改主机，开机自启。**

## 背景

在 Xiaomi Pad 5 Pro 12.4（Android 12 / MIUI，arm64-v8a，KernelSU/SukiSU）上，UU远程 远程桌面里的鼠标滚轮方向是反的。最终在 UU远程主机 4.45 上验证：v1.3+ 的稳定虚拟设备配合 `invert=0` 直通，远程与平板本地滚动都正常。v1.3 起虚拟鼠标在物理鼠标重连时保持存在，避免 UU远程 绑定到已销毁的旧设备。

Frida 钩 `MotionEvent.getAxisValue(AXIS_VSCROLL)` 取反可以临时修复，但需要主机 watcher，重启后容易失效。本项目把修复下沉到 Android 输入层。

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
- `dist/uu-wheel-evdev.zip`：KernelSU/Magisk 模块包

手动编译：

```bash
zig cc -target aarch64-linux-musl -static -O2 -o uu_wheel_flip src/uu_wheel_flip.c
```

## 安装

```bash
adb push dist/uu-wheel-evdev.zip /data/local/tmp/
adb shell "su -c 'ksud module install /data/local/tmp/uu-wheel-evdev.zip'"
adb reboot
```

Magisk：

```bash
adb shell "su -c 'magisk --install-module /data/local/tmp/uu-wheel-evdev.zip'"
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

- `0`：直通，不翻转（v1.4 默认，当前验证正确）
- `1`：翻转 `REL_WHEEL` / `REL_HWHEEL`

修改后**立即生效**，不需要重启 daemon。日志：

```bash
adb shell "su -c 'cat /data/local/tmp/uu_wheel_flip.log'"
```

## 注意

- 目前只接管名字含 `BT5.0 Mouse` 或 `Xiaomi Mouse` 的设备。换鼠标需要改 `src/uu_wheel_flip.c` 里的 `is_target()`。
- `service.sh` 只在控制文件不存在时写入默认值。从旧版升级且已有控制文件时不会被覆盖；如果之前手动设过 `1`，升级后请执行上面的 `echo 0`。
- 开机后 daemon 有约 15 秒启动延迟；如果刚开机就打开 UU远程，可能先绑到物理鼠标，重开一次 App 即可。
- BT 鼠标在开机后可能重连一次；v1.3 会保留虚拟鼠标并复用，不需要因为重连重开 UU远程。
- **不要同时运行主机端的 `WheelFlip.exe`**。它会把 UU远程 注入的滚轮再翻一次，和本方案形成双重取反。本项目测试时已将其禁用。
- 如果之前装过 Frida 的 `uu-wheel-fix` 模块，请先卸载，避免残留的 frida-server 干扰。
- 不同 ROM/输入栈可能有差异；如果方向不对，先尝试切换 `invert`，再检查是否有其他软件在改滚轮方向。

## License

MIT
