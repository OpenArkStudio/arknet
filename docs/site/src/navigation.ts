import type { BadgeVariant, Breadcrumb, SidebarBadge, SidebarItem } from "@cloudflare/nimbus-docs/types";

type Badge = { en: string; zh: string; variant: BadgeVariant };
type Page = [slug: string, en: string, zh: string, badge?: Badge];
type Group = { en: string; zh: string; children: Entry[]; open?: boolean; badge?: Badge };
type Entry = Page | Group;
const protocols: Page[] = [
  ["tcp", "TCP", "TCP"], ["udp", "UDP", "UDP"],
  ["rudp", "RUDP", "RUDP", { en: "Experimental", zh: "实验性", variant: "warning" }],
  ["websocket", "WebSocket / WSS", "WebSocket / WSS"],
  ["http", "HTTP / HTTPS", "HTTP / HTTPS"], ["tls", "TLS / mTLS", "TLS / mTLS"],
];
const tree: Entry[] = [
  ["", "Overview", "概览"],
  { en: "Get started", zh: "开始使用", open: true,
    badge: { en: "Start here", zh: "从这里开始", variant: "success" }, children: [
    ["get-started/installation", "Install and configure", "安装与配置"],
    ["get-started/first-application", "First client and server", "第一个客户端与服务端"],
  ] },
  { en: "Concepts", zh: "核心概念", children: [
    ["concepts", "Overview", "概览"],
    ["concepts/endpoints", "Clients, servers and sessions", "客户端、服务端与 Session"],
    ["concepts/io-models", "IO models and coroutines", "IO 模型与协程"],
    ["concepts/lifecycle", "Lifecycle and execution", "生命周期与执行上下文"],
    ["concepts/sending", "Sending, ownership and backpressure", "发送、所有权与背压"],
  ] },
  { en: "Features", zh: "功能", open: true, children: [
    ["features", "Capabilities and support", "能力与支持状态"],
    ...protocols.map(([slug, en, zh, badge]): Group => ({ en, zh, badge, children: [
      [`features/${slug}`, "Overview", "概览"],
      [`features/${slug}/server`, "Create a server", "创建服务端"],
      [`features/${slug}/client`, "Create a client", "创建客户端"],
      ...(slug === "http" ? [["features/http/requests", "Send a single request", "发送单次请求"] as Page] : []),
      ...(slug === "rudp" ? [["features/rudp/fec", "Optional FEC design", "可选 FEC 设计", { en: "TODO", zh: "TODO", variant: "warning" }] as Page] : []),
      [`features/${slug}/configuration`, "Configuration and advanced usage", "配置与进阶使用"],
      [`features/${slug}/performance`, "Performance", "性能"],
    ] })),
  ] },
  { en: "Guides", zh: "使用指南", children: [
    ["guides", "Overview", "概览"],
    ["guides/socket-buffers", "Configure socket buffers", "配置 socket 缓冲"],
    ["guides/external-io", "Use an external IO context", "接入外部 IO context"],
    ["guides/reconnect", "Configure automatic reconnect", "配置自动重连"],
    ["guides/timers", "Use timers and post", "使用定时器与 post"],
    ["guides/http-routing", "HTTP routing and middleware", "HTTP 路由与中间件"],
    ["guides/ipv4-ipv6", "Use IPv4 and IPv6", "使用 IPv4 与 IPv6"],
  ] },
  ["examples", "Examples", "示例"],
  { en: "Reference", zh: "参考", badge: { en: "API", zh: "API", variant: "info" }, children: [
    ["reference/api", "Public API index", "公共 API 索引"],
    ["reference/configuration", "Build options and defaults", "构建选项与默认值"],
    ["reference/errors", "Errors and TLS verification", "错误处理与 TLS 校验"],
  ] },
  { en: "Performance", zh: "性能", children: [
    ["performance", "Reports and measurement scope", "报告与测量范围"],
    ["performance/rudp", "RUDP", "RUDP"],
    ["performance/methodology", "Methodology and metrics", "测量方法与指标"],
    ["performance/io-comparison", "IO and coroutine comparison", "IO 与协程对比"],
    ["performance/dimensions-native-tcp", "Matched callbacks, coroutines and CPU", "同负载回调、协程与 CPU"],
    { en: "UDP", zh: "UDP", children: [
      ["performance/udp-update", "Results and buffer diagnosis", "结果与缓冲诊断"],
      ["performance/udp", "IO models", "IO 模型"],
      ["performance/dimensions-udp", "Payload and in-flight requests", "包体与在途数量"],
      ["performance/udp-limits", "Datagram limits and loss", "数据报边界与丢失"],
    ] },
    { en: "Protocol comparisons", zh: "协议对比", children: [
      ["performance/overview", "Multi-protocol summary", "多协议汇总"],
      ["performance/dimensions", "Payload, batch and execution", "包体、批量与执行方式"],
      ["performance/coroutine", "Native coroutine IO models", "原生协程 IO 模型"],
      ...["tcp", "tcps", "websocket", "wss", "http", "https"].map((slug): Group => ({
        en: slug === "tcps" ? "TLS TCP" : slug === "websocket" ? "WebSocket" : slug.toUpperCase(),
        zh: slug === "tcps" ? "TLS TCP" : slug === "websocket" ? "WebSocket" : slug.toUpperCase(), children: [
          [`performance/${slug}`, "IO models", "IO 模型"],
          [`performance/dimensions-${slug}`, "Payload and batches", "包体与批量"],
        ],
      })),
    ] },
  ] },
  { en: "Project", zh: "项目", children: [
    ["project", "Overview", "概览"],
    ["project/testing", "Testing and validation", "测试与验证"],
    ["project/troubleshooting", "Troubleshooting", "故障排查"],
    ["project/architecture", "Architecture and extension", "架构与扩展"],
    ["project/contributing", "Contributing and code style", "贡献与代码规范"],
    ["project/roadmap", "Roadmap and TODO", "路线图与 TODO", { en: "TODO", zh: "TODO", variant: "warning" }],
    ["project/third-party-notices", "Third-party notices", "第三方声明"],
  ] },
];

export function navigation(chinese: boolean, current: string): SidebarItem[] {
  const prefix = chinese ? "/zh/" : "/";
  function badge(value?: Badge): SidebarBadge | undefined {
    return value ? { text: chinese ? value.zh : value.en, variant: value.variant } : undefined;
  }
  function items(entries: Entry[]): SidebarItem[] {
    return entries.map((entry, order): SidebarItem => Array.isArray(entry) ? {
      type: "link", label: entry[chinese ? 2 : 1], href: prefix + entry[0], order,
      badge: badge(entry[3]),
      isCurrent: current.replace(/\/$/, "") === (prefix + entry[0]).replace(/\/$/, ""),
    } : {
      type: "group", label: chinese ? entry.zh : entry.en, order,
      badge: badge(entry.badge),
      collapsed: !entry.open, children: items(entry.children),
    });
  }
  return items(tree);
}

export function pageBreadcrumbs(items: SidebarItem[], title: string, chinese: boolean): Breadcrumb[] {
  function find(entries: SidebarItem[]): Breadcrumb[] | undefined {
    for (const item of entries) {
      if (item.type === "link" && item.isCurrent) return [{ label: title }];
      if (item.type !== "group") continue;
      const path = find(item.children);
      if (path) {
        const overview = item.children.find(child => child.type === "link");
        return [{ label: item.label, href: overview?.type === "link" ? overview.href : undefined }, ...path];
      }
    }
  }
  return [{ label: "arknet", href: chinese ? "/zh/" : "/" }, ...(find(items) ?? [{ label: title }])];
}
