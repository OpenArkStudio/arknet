"""Format maintainer reports without reading, deriving or changing measurements.

Both report generators call ``format_report``. The same idempotent operation can
refresh presentation in historical Markdown while preserving its original data,
metadata and extra sections, without recreating missing raw measurements.
Refresh historical presentation with:
    python3 benchmarks/report_presentation.py docs/performance
"""

from html import escape
from pathlib import Path
import re


# Only known explanatory paragraphs are rewritten. Tables, fenced commands,
# historical metadata and unrecognized prose are kept verbatim; findings retain
# their computed values and units while repeated explanatory wording shrinks.
PROSE = (
    ("Measurements come from independent local Release loopback processes, not CI.",
     "Independent local Release loopback measurements; collected outside CI."),
    ("数据来自本机 Release 构建的独立回环进程，不来自 CI。", "本机 Release 独立回环进程测量，非 CI 数据。"),
    ("The native Asio TCP coroutine test program compares no added computation with 10,000 CPU iterations per echo. Public coroutine endpoint APIs remain TODO.",
     "Native Asio TCP coroutines: no added computation versus 10,000 CPU iterations per echo. Public coroutine endpoints remain TODO."),
    ("使用原生 Asio TCP 协程测试程序，比较无额外业务计算和每次回显执行 10,000 次 CPU 计算两种负载。公开协程端点 API 仍为 TODO。",
     "原生 Asio TCP 协程比较无额外计算与每次回显 10,000 次 CPU 计算；公开协程端点 API 仍为 TODO。"),
    ("Tables show repeated-run medians; throughput brackets show min/max, not confidence intervals. Latencies are medians of per-run sampled percentiles, without pooling samples.",
     "Run medians; throughput brackets are min/max, not confidence intervals. Latencies: median of per-run sampled percentiles, without pooling."),
    ("表格记录重复运行的中位数，吞吐量方括号为最小值和最大值；误差范围不是置信区间。延迟取各次采样分位数的中位数，不合并样本。",
     "表格为各轮中位数；吞吐括号为最小／最大值，非置信区间。延迟为各轮采样分位数的中位数，不合并样本。"),
    ("The highest median applies only to the stated load; complete results appear below.",
     "Highest medians apply only to the stated load; see protocol reports."),
    ("最高中位数仅适用于对应负载；完整数据见下表。", "最高中位数仅适用于对应负载；完整数据见协议报告。"),
    ("Detailed pages retain all 64/1024/16384-byte and 1/16-client profiles. Bars show repeated-run medians; error bars show min/max, not confidence intervals.",
     "Detailed pages retain all 64/1024/16384-byte and 1/16-client profiles. Bars: repeated-run medians; whiskers: min/max, not confidence intervals."),
    ("详细页包含 64/1024/16384 字节、1/16 个客户端的全部数据。柱子为重复运行的中位数；误差线为最小／最大值，不是置信区间。",
     "详细页保留全部 64/1024/16384 字节、1/16 客户端数据；柱高中位数、误差线最小／最大值，非置信区间。"),
    ("Both executions use native Asio and fixed-size TCP batches: the server reads a complete batch, applies the same computation to each message, then writes the entire reply. Window 1 records message RTT; larger windows record batch RTT. The public arknet coroutine endpoint API remains TODO.",
     "Native Asio callbacks/coroutines share fixed-size TCP batches: the server reads a full batch, applies the same computation per message, then writes the entire reply. Window 1 records message RTT; larger windows batch RTT. Public arknet coroutine endpoints remain TODO."),
    ("回调与协程都使用原生 Asio 和固定长度 TCP 批次：服务端完整读取一批，逐条执行相同计算后整批回写。窗口为 1 时记录单消息 RTT，更大窗口记录批次 RTT。arknet 公开协程端点 API 仍为 TODO。",
     "原生 Asio 回调／协程使用相同固定长度 TCP 批次：服务端读完整批、逐条执行相同计算，再整批回写。窗口 1 测单消息 RTT，更大窗口测批次 RTT；arknet 公开协程端点 API 仍为 TODO。"),
)

PROSE += (
    ("No failed profiles for this protocol.", "No failed profiles."),
    ("本协议没有失败配置。", "没有失败配置。"),
    ("16 clients; 64/1024/16384-byte messages; windows 1/4/16/64; three repetitions per configuration.",
     "16 clients; 64/1024/16384 bytes; windows 1/4/16/64; three runs/configuration."),
    ("16 个客户端；消息为 64/1024/16384 字节，窗口为 1/4/16/64；每个配置重复三次。",
     "16 客户端；64/1024/16384 字节，窗口 1/4/16/64；每配置三次。"),
)

# Source text, shorter text, callout title, callout type. The scope and missing-
# sample rules remain visible; only large exact profile tables are optional.
CAVEATS = (
    ("With window>1, RTT measures batch completion; the callback program measures individual messages. These results cannot establish coroutine speedups.",
     "With window>1, coroutines measure batch RTT; callbacks measure messages. These results cannot establish coroutine speedups.", "RTT scope", "caution"),
    ("window>1 时测量整批 RTT；回调测试程序测量逐消息 RTT，两者不能用来推导协程加速比。",
     "window>1 时协程测整批 RTT，回调测逐消息 RTT，不能据此推导协程加速比。", "RTT 范围", "caution"),
    ("Coroutine window=16 measures batch RTT; the callback program measures message RTT. These results do not establish coroutine speedups.",
     "Coroutine window=16 measures batch RTT; callbacks measure message RTT. This does not establish coroutine speedups.", "RTT scope", "caution"),
    ("协程 window=16 测量整批 RTT，回调测试程序测量逐消息 RTT；不能据此判断协程加速比。",
     "协程 window=16 测整批 RTT，回调测逐消息 RTT；不能据此判断协程加速比。", "RTT 范围", "caution"),
    ("两种依赖方案使用的 Beast 版本不同，方案之间的性能差异同时包含依赖版本的影响，不能全部归因于 Asio。",
     "两种方案的 Beast 版本不同，性能差异包含依赖版本影响，不能全部归因于 Asio。", "依赖版本", "caution"),
    ("Local loopback results do not establish cross-host or production capacity. Short runs, scheduling and thermals introduce variance; repeat with the deployment workload.",
     "Loopback does not establish cross-host or production capacity. Short runs, scheduling and thermals add variance; repeat with the deployment workload.", "Scope", "caution"),
    ("本机回环结果不代表跨主机或生产容量。短时测量、调度和温度可能带来波动；部署前需使用实际负载测量。",
     "回环不证明跨主机或生产容量；短时测量、调度与温度可带来波动，部署前需测实际负载。", "适用范围", "caution"),
    ("If any repetition fails, all three measurements for that profile are excluded from rate, latency and matched-ratio summaries; raw failures are retained.",
     "Any failed repetition excludes the profile’s three runs from rate, latency and matched-ratio summaries. Raw failures remain.", "Failed profiles", "caution"),
    ("任一次重复失败，该配置的三次测量均不进入吞吐量、延迟或匹配倍率统计；原始数据保留失败记录。",
     "任一次重复失败，该配置三次测量均不计吞吐量、延迟或匹配倍率；原始失败记录保留。", "失败配置", "caution"),
    ("Larger UDP windows can lose traffic; profiles with loss are failed measurements, not capacity results. The server still receives through one serialized socket.",
     "Larger UDP windows may lose traffic; such profiles are failed measurements, not capacity results. Server reception still uses one serialized socket.", "UDP limits", "caution"),
    ("UDP 窗口增加可能导致丢失；出现丢失的配置属于失败测量，不能作为吞吐能力。服务端仍通过一个 socket 串行接收。",
     "增大 UDP 窗口可能丢失；丢失配置属于失败测量，不代表吞吐能力。服务端仍用一个 socket 串行接收。", "UDP 限制", "caution"),
    ("Local loopback, fixed connection distributions and short runs do not establish production capacity; repeat with real messages and connection activity.",
     "Loopback, fixed connections and short runs do not establish production capacity; repeat with real messages and connection activity.", "Scope", "caution"),
    ("本机回环、固定连接分布和短时测量不代表生产容量，业务选型仍需使用实际消息与连接分布测量。",
     "回环、固定连接分布与短时测量不证明生产容量；选型需测实际消息与连接分布。", "适用范围", "caution"),
)

CAVEATS += (
    ("The dependency combinations use different Beast versions, so differences between them also include dependency-version effects and cannot be attributed solely to Asio.",
     "Different Beast versions also affect these combinations; differences cannot be attributed solely to Asio.", "Dependencies", "caution"),
    ("The arknet protocol program measures message RTT; matched native Asio callback/coroutine executions measure batch RTT. Their fixed-batch results do not establish speedups over arknet endpoints; public coroutine endpoint APIs remain TODO.",
     "Arknet protocols measure message RTT; matched native Asio callbacks/coroutines measure batch RTT. Fixed batches cannot establish speedups over arknet endpoints; public coroutine APIs remain TODO.", "RTT scope", "caution"),
    ("arknet 协议测试记录单消息 RTT；原生 Asio 的回调／协程匹配测试记录批次 RTT。后者的固定批次数据不能与前者直接组成协程加速比；公开协程端点 API 仍为 TODO。",
     "arknet 协议测单消息 RTT，原生 Asio 回调／协程匹配测试测批次 RTT；固定批次不能据此推导相对 arknet 端点的协程加速比，公开协程端点 API 仍为 TODO。", "RTT 范围", "caution"),
    ("Throughput and CPU ratios pair repetitions at the same dependency combination, payload, batch size, thread model and computation before taking the median; 1 means equal values. Lower CPU utilization does not necessarily complete more requests; compare throughput too. If any CPU sample is missing, its CPU summary and matched ratio are N/A while throughput may remain valid. These results do not predict arknet callback endpoint versus future coroutine API speed.",
     "Pair repetitions with the same dependency combination, payload, batch, thread model and computation, then take throughput/CPU ratio medians; 1 means equal. Lower CPU need not complete more requests; compare throughput. Any missing CPU sample makes CPU summaries/ratios N/A; throughput may remain valid. This cannot predict arknet callback versus future coroutine API speed.", "Ratios and limits", "caution"),
    ("吞吐和 CPU 倍率按相同依赖方案、消息、批次大小、线程模型和计算量逐次配对后取中位数；1 表示相同。CPU 占用更低不一定处理更多请求，需结合吞吐倍率判断。CPU 缺少任一次采样时，CPU 统计和对应倍率为 N/A，吞吐统计仍可保留。该结果不能推算 arknet 回调端点与未来协程 API 的速度差异。",
     "同依赖方案、消息、批次、线程模型和计算量逐次配对，再取吞吐／CPU 倍率中位数；1 表示相同。CPU 更低不一定处理更多请求，需结合吞吐。缺任一次 CPU 样本时，其统计与倍率为 N/A，吞吐可保留；不能推算 arknet 回调端点与未来协程 API 的速度差异。", "倍率与边界", "caution"),
)

CPU = (
    ("Average process CPU uses 100% for one logical CPU and can exceed 100% with multiple threads; estimated values are labeled, and incomplete sets of three CPU samples show N/A.",
     "Process CPU: 100% per logical CPU; multiple threads can exceed 100%. Estimates are labeled; incomplete three-sample sets show N/A."),
    ("平均进程 CPU 占用率以单核为 100%，多线程可超过 100%；估算值单独标注，三次 CPU 采样不完整时为 N/A。",
     "进程 CPU 单核为 100%，多线程可超过 100%；标注估算值，三次采样不全记 N/A。"),
)


def aside(body, title, kind="caution"):
    return f'<Aside type="{kind}" title="{title}">\n\n{body}\n\n</Aside>'


def format_report(markdown, kind, chinese):
    """Return idempotent Nimbus presentation; measurements are opaque Markdown."""
    if kind not in {"protocol", "overview", "dimension", "dimension-index"}:
        raise ValueError(f"unknown report kind: {kind}")
    replacements = dict(PROSE)
    caveats = {old: (new, title, tone) for old, new, title, tone in CAVEATS}
    protected = r'(?ms)(^```[^\n]*\n.*?^```[^\n]*|<Aside\b.*?</Aside>|<details\b.*?</details>)'

    def plain(source):
        paragraphs = re.split(r'(\n\n+)', source)
        for index in range(0, len(paragraphs), 2):
            paragraph = paragraphs[index]
            flat = " ".join(paragraph.split())
            if flat in replacements:
                paragraphs[index] = replacements[flat]
            elif flat in caveats:
                body, title, tone = caveats[flat]
                paragraphs[index] = aside(body, title, tone)
            else:
                for old, new in CPU:
                    if flat.startswith(old):
                        paragraphs[index] = aside(new + flat[len(old):], "CPU", "note")
                        break
        source = "".join(paragraphs)
        # Findings already contain computed results. Shorten only their repeated
        # prose, retaining each value, unit, dependency and matched topology.
        source = re.sub(r"(?m)^- (?:Standalone Asio|Boost\.Asio): [^\n]*$", lambda match: match[0]
                        .replace(": At ", ", ").replace(" bytes, the highest throughput median uses ", " bytes: highest throughput median — ")
                        .replace("; the lowest p99 median uses ", "; lowest p99 median — ")
                        .replace("Matched coroutine/callback throughput medians range from ", "Matched coroutine/callback throughput median range: ")
                        .replace("Matched coroutine/callback CPU medians range from ", "Matched coroutine/callback CPU median range: ")
                        .replace("× to ", "×–").replace("Compare with the throughput ratio.", "Compare throughput.")
                        .replace(" 字节时，吞吐量中位数最高为 ", " 字节：吞吐量中位数最高 ")
                        .replace("；p99 中位数最低为 ", "；p99 中位数最低 ")
                        .replace("匹配配置的协程／回调吞吐倍率中位数范围为 ", "匹配协程／回调吞吐倍率中位数：")
                        .replace("匹配配置的协程／回调 CPU 占用倍率中位数范围为 ", "匹配协程／回调 CPU 倍率中位数："), source)
        if kind == "protocol":
            profiles = r'(?m)(?:^#### [^\n]+\n\n(?:\|[^\n]*\n)+\n*)+'
            # A disclosure contains only the exact-value profile headings/tables.
            # Charts, legends, missing-data notes and unrelated historical sections
            # remain outside it. Existing disclosures were protected above.
            title = "完整数值" if chinese else "Full values"
            source = re.sub(profiles, lambda match: f'<details>\n<summary>{title}</summary>\n\n{match[0].rstrip()}\n\n</details>\n\n', source)
        if kind == "overview":
            routes = r'(?m)(?:^- \[[^\]]+\]\((?:\./)?(?:tcp|udp|websocket|tcps|wss|http|https|coroutine)(?:_CN)?\.md\)\n?)+'
            def cards(match):
                items = re.findall(r'- \[([^\]]+)\]\(([^)]+)\)', match[0])
                return '<CardGrid>\n' + '\n'.join(f'<LinkCard title="{escape(label, quote=True)}" href="{escape(href, quote=True)}" />' for label, href in items) + '\n</CardGrid>\n'
            source = re.sub(routes, cards, source)
        return source

    chunks = re.split(protected, markdown)
    result = "".join(chunk if index % 2 else plain(chunk) for index, chunk in enumerate(chunks))
    return result + ("\n" if markdown.endswith("\n") and not result.endswith("\n") else "")


def refresh(directory):
    """Refresh only maintained Markdown presentation, never SVG/JSON/raw inputs."""
    names = {"overview": "overview", **{name: "protocol" for name in
             ("tcp", "udp", "websocket", "tcps", "wss", "http", "https", "coroutine")},
             "dimensions": "dimension-index", **{"dimensions-" + name: "dimension" for name in
             ("tcp", "udp", "websocket", "tcps", "wss", "http", "https", "native-tcp")}}
    for name, kind in names.items():
        for chinese in (False, True):
            path = directory / (name + ("_CN" if chinese else "") + ".md")
            source = path.read_text(encoding="utf-8")
            updated = format_report(source, kind, chinese)
            if updated != source:
                path.write_text(updated, encoding="utf-8")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="existing report Markdown directory")
    refresh(parser.parse_args().directory)
