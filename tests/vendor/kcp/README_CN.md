# KCP 测试参考实现

[English](README.md)

互通测试编译 [skywind3000/kcp](https://github.com/skywind3000/kcp) 的官方 C 实现，
固定版本为提交
[`b1a7a2101dcbb96017681a500d6b82bbe5a88766`](https://github.com/skywind3000/kcp/tree/b1a7a2101dcbb96017681a500d6b82bbe5a88766)。
源文件未经修改。

这些文件仅用作测试对端，不随 arknet 安装，也不会被公共头文件包含。
arknet 的 C++ 协议实现不依赖这些源码或原有 zephyr 集成。

保留上游 MIT [许可证](LICENSE)、版权声明和源文件换行格式。
导入文件对应的上游 Git blob：

| 文件 | Git Blob |
| --- | --- |
| `ikcp.c` | `1f32039d9ac6bc80164b3241bacf6622fa3a34e6` |
| `ikcp.h` | `a7447c16946b2f4fc78572b325d4b924401bb42c` |
| `LICENSE` | `4f9f28fdcb8cb5d988e684a4503fc2fad29ca2d5` |

检查导入文件是否保持原始内容：

```sh
git hash-object --no-filters tests/vendor/kcp/ikcp.c tests/vendor/kcp/ikcp.h tests/vendor/kcp/LICENSE
```

`tests/arq_interop.cpp` 使用可复现的丢包、重复包和乱序场景测试双向消息传输，
并检查 ACK 以及零窗口下的 WASK/WINS 交互。
UDP 会话握手不属于 KCP 核心线协议，由会话测试单独验证。

上游的 `IOFFSETOF` 宏通过空指针的成员表达式计算结构偏移。
Clang 的对象大小检查也会报告这些表达式，因此 UBSan 构建仅对 `ikcp.c`
关闭空指针和对象大小检查。arknet 自有代码保留完整检查，上游文件的其他检查仍然启用。
这样可以保持官方测试源码不变。
