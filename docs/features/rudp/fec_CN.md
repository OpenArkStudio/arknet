# FEC 策略

RUDP 默认只使用 ARQ。可选的 Reed–Solomon FEC 在发送数据报的同时增加校验分片，
接收端先尝试恢复缺失分片，未恢复的数据仍由 ARQ 重传。编码引擎内置于头文件，
无需 Python 或额外链接库。

```mermaid
flowchart LR
    A[应用消息] --> B[ARQ 分片与确认]
    B --> C[FEC 数据与校验分片]
    C --> D[UDP]
    D --> E[FEC 恢复]
    E --> F[ARQ 排序与重组]
    F --> G[完整消息]
```

## 选择策略

| 策略 | 设置 | 适用情况 |
| --- | --- | --- |
| 仅 ARQ，默认 | `fec::options{}` | 丢失较少，优先控制 CPU 和额外带宽 |
| 固定 FEC | `fec::options::fixed()` | 已测量链路特征，需要可预测的恢复能力 |
| 自适应 FEC | `fec::options::adaptive()` | 链路变化明显，按预设阈值增减冗余 |

游戏、控制消息和服务通信可以采用相同策略。选择依据是恢复延迟、实际丢失、
消息密度和资源预算，而不是业务所属行业。稀疏流量无法形成有效分组时不发送校验。

```cpp
#include <arknet/rudp/rudp_client.hpp>
#include <arknet/rudp/rudp_server.hpp>

arknet::rudp_server server;
arknet::rudp_client client;
auto settings = arknet::fec::options::adaptive();
settings.max_redundancy_percent = 50;
server.set_fec_options(settings);
client.set_fec_options(settings);
```

两端在停止状态配置。固定与自适应策略可以互通，每组携带实际参数；
两端不必采用同一发送策略。启用 FEC 的服务端也接受仅 ARQ 的客户端。
请求 FEC 的客户端遇到未启用 FEC 的 arknet 服务端，握手返回 `operation_not_supported`。
旧服务端无法识别新握手时，连接超时，不会静默降级。

## 编码与缓存

| 参数 | 默认值 | 行为 |
| --- | ---: | --- |
| `data_shards` | 8 | 每组最多 8 个数据报，可配置 1–32 |
| `parity_shards` | 2 | 固定策略的校验分片数 |
| `max_parity_shards` | 4 | 自适应校验上限，可配置 1–16 |
| `min_data_shards` | 2 | 小于此数量的组不生成校验 |
| `group_wait_ms` | 2 ms | 最长组包等待；ACK 和窗口控制数据报直接发送 |
| `max_redundancy_percent` | 50% | 每组校验报文字节相对数据报文字节的上限，包含 FEC 头部和填充 |
| `group_lifetime_ms` | 1000 ms | 未完成接收组的保留期限 |
| `max_groups` | 128 | 未完成接收组的数量上限 |
| `max_buffer_bytes` | 1 MiB | 发送待组包数据与接收组的合计保留预算 |

8 个数据分片加 2 个校验分片，任意 8 个分片到达即可恢复全部数据。
这是组内分片数量的保证，不是任意丢包率的保证；一次突发丢失超过 2 个分片时仍需重传。
实际校验数还受字节预算和最小分组大小约束，可能低于配置值。

完整组立即释放，未完成组到期释放。缓存不足时停止保留额外 FEC 分片，
合法原始数据继续交给 ARQ，不关闭可靠交付机制。重复过滤使用固定大小的 4096 组窗口。
编解码临时空间受分片数量和 MTU 限制，另计于保留预算之外；并非无限制缓存。

`arq::options::mtu` 在 FEC 模式下仍表示 UDP 载荷上限。握手采用两端配置的较小值，
ARQ 从该值中扣除 **26 字节** FEC 开销；例如 1200 字节上限对应 1174 字节 ARQ 数据报。
消息最大分片数仍为 256。设置较大 MTU 不代表路径支持该大小，参见[缓冲配置](../../guides/socket-buffers_CN.md)。

## 自适应阈值

| 参数 | 默认值 | 行为 |
| --- | ---: | --- |
| `sample_window_ms` | 1000 ms | 本地重传与接收端反馈的统计周期 |
| `min_samples` | 64 | 样本不足时保持当前参数 |
| `enable_loss_percent` | 3% | 达到阈值时增加校验数 |
| `disable_loss_percent` | 1% | 低于阈值时累计稳定窗口 |
| `stable_windows` | 3 | 连续稳定窗口达到此数量后减少一个校验分片 |
| `cooldown_ms` | 2000 ms | 常规调整之间的最短间隔 |
| `overload_queue_percent` | 75% | 队列达到阈值时关闭冗余，不等待冷却结束 |

自适应策略从零校验开始。参考本地重传比例与对端缺失反馈，取较高的比例；
按观察比例选择校验数，受数量和字节预算共同限制。RTT 超过已观察最小值的两倍，
且排队字节持续增长时，也关闭冗余。ARQ 拥塞控制继续生效。

<Aside type="caution" title="增加冗余不能解决过载">

先处理持续增长的发送队列、接收缓冲溢出和应用处理积压，再评估 FEC。
组内缺失可能包含乱序造成的迟到，反馈不能单独确定丢包原因。

</Aside>

## 读取统计

```cpp
// 在仍运行的 IO context 外等待，不能在端点回调中阻塞。
auto pending = client.get_rudp_statistics();
auto stats = pending.get();
auto restored = stats.fec.recovered_shards;
auto retries = stats.retransmissions;
auto parity = stats.fec.current_parity_shards;
auto reason = arknet::fec::reason_name(stats.fec.last_change);
```

Client 和 Session 均在已连接状态提供异步快照；未连接时查询抛出 `std::logic_error`。
查询期间 IO context 必须继续运行，停止可取消尚未完成的查询。重连创建新的统计周期。
`last_change` 为 `configured`、`loss`、
`healthy` 或 `overload`，`policy_changes` 为调整次数。

`missing_shards` 统计完整组恢复时或未完成组到期时仍未到达的原始分片，
包含被 FEC 恢复的分片，也可能包含后来才到达的原始分片。
`recovered_shards` 统计通过校验恢复的原始分片。`retransmissions` 是 ARQ segment 重传次数。
三个计数单位不同，不能直接相减得到最终丢失。最终交付应检查应用消息内容、顺序和完成情况。

## 兼容与安全

FEC 使用版本 1 的协商和 `AFEC` 数据报。仅 ARQ 模式保持现有 KCP 消息模式与
ARK RDP 连接报文兼容；FEC 模式要求两端支持 arknet FEC，不能直接连接裸 KCP。
CRC 检测传输或恢复后的损坏，不提供认证、加密或恶意数据防护。
部署在不可信网络时，应用必须另行处理身份验证与访问控制。

查看[配置](configuration_CN.md)了解会话限额与失败行为，查看[性能](performance_CN.md)了解实测范围。
