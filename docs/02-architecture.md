# 分层架构

## 层次

```
┌──────────────────────────────────────────────────────────────┐
│ app/         组合根：构造驱动与服务，注入依赖                 │
├──────────────────────────────────────────────────────────────┤
│ controller/  参数解析、运行模式（动画 / 纯报告 / 跳过）        │
├──────────────────────────────────────────────────────────────┤
│ service/     业务：配置装载、自检、动画原语、序列编排、会话状态 │
├──────────────────────────────────────────────────────────────┤
│ model/       纯数据与纯函数：颜色、系统快照、检查结果、配置结构 │
├──────────────────────────────────────────────────────────────┤
│ driver/      外部世界：ANSI 终端、时钟、Linux 探针             │
├──────────────────────────────────────────────────────────────┤
│ utils/       UTF-8 文本宽度、ANSI 构造、日志、字符切分          │
└──────────────────────────────────────────────────────────────┘
```

依赖只能向下：`service` 依赖 `driver` 的**接口**（`ITerminal` / `IClock` / `ISystemProbe`），
而不是它们的实现；`driver` 不依赖 `service`。这样测试里可以整体替换 I/O。

## 关键接口

| 接口 | 实现 | 用途 |
| --- | --- | --- |
| `driver::ITerminal` | `AnsiTerminal` / `FakeTerminal` | 输出字节、光标与清屏、尺寸、颜色能力 |
| `driver::IClock` | `SteadyClockDriver` / `FakeClock` | 单调毫秒、本地时间格式化、睡眠 |
| `driver::ISystemProbe` | `LinuxSystemProbe` / `FakeProbe` | 按域采集内核、主机、CPU、内存、存储、网络、会话 |

## 数据流

```
main
 └─ ConfigService::loadDefaultLocations() → BootSettings
     ├─ AnsiTerminal ─┐
     ├─ SteadyClock  ─┼→ AnimationService（逐字、覆盖行、光标移动）
     ├─ LinuxProbe   ─┼→ SelfcheckService（7 个域，各自计时）
     └─ SessionState ─┴→ BootSequenceService（版式 + 时序）
                          └─ BootController::run() → 退出码
```

`BootSequenceService` 只做「排版 + 节奏」：从哪里取数据、怎么计时、怎么落盘都不归它管。
换一套视觉风格只需要改它和 `LogoArt`，数据与 I/O 不动。

## 自检域

| 域 | 数据来源 | 告警阈值 |
| --- | --- | --- |
| KERNEL | `uname(2)` | 无 |
| HOST | `gethostname`、`/proc/device-tree/model` | 无 |
| CPU | `/proc/cpuinfo`、`cpufreq`、`thermal_zone0`、`/proc/loadavg` | ≥70°C 告警，≥80°C 失败 |
| MEMORY | `/proc/meminfo` | 可用 <15% 告警，<8% 失败 |
| STORAGE | `statvfs("/")` | 可用 <15% 告警，<5% 失败 |
| NETWORK | `getifaddrs`、`/proc/net/route` | 无 IPv4 告警 |
| SESSION | `/proc/[pid]/{comm,stat}`、`/proc/uptime` | sshd 不可见告警 |

全部读取 procfs/sysfs，不 `fork` 子进程，单次自检在百微秒级。

## 扩展点

- 新增一条自检项：在 `ProbeDomain` 加枚举 → `LinuxSystemProbe` 填值 → `SelfcheckService` 组行。
- 新增视觉元素：在 `BootSequenceService` 增加 render 方法，复用 `AnimationService` 原语。
- 换成图形前端：实现 `ITerminal` 的另一个版本即可，`service` 层无需改动。
