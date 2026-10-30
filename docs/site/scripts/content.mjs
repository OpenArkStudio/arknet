import { unified } from "unified";
import remarkParse from "remark-parse";

const parser = unified().use(remarkParse);

export function pageRoute(file) {
  const chinese = file.endsWith("_CN.md");
  let slug = file.replace(/(?:_CN)?\.md$/, "");
  if (slug === "README") slug = "";
  if (slug === "THIRD_PARTY_NOTICES") slug = "third-party-notices";
  slug = slug.replace(/\/index$/, "");
  return (chinese ? "/zh/" : "/") + slug;
}

function text(node) {
  return node.value ?? (node.children ?? []).map(text).join("");
}

function components(tree) {
  const names = new Set();
  function walk(node) {
    if (node.type === "html") {
      for (const match of node.value.matchAll(/<(Aside|Steps|Step|CardGrid|LinkCard|Tabs|TabItem)(?=[\s/>])/g)) names.add(match[1]);
    }
    for (const child of node.children ?? []) walk(child);
  }
  walk(tree);
  return names;
}

export function pageExtension(source) {
  return components(parser.parse(source)).size ? ".mdx" : ".md";
}

export function convertPage(source, file, files) {
  const tree = parser.parse(source);
  const heading = tree.children.find((node) => node.type === "heading" && node.depth === 1);
  if (!heading) throw new Error(`${file}: missing page heading`);
  const title = text(heading);
  const replacements = [{ start: heading.position.start.offset, end: heading.position.end.offset, value: "" }];
  function destinationFor(href, image = false) {
    const url = new URL(href, `https://source.invalid/docs/${file}`);
    const pathname = decodeURIComponent(url.pathname);
    let destination;
    if (!pathname.startsWith("/docs/")) {
      destination = "https://github.com/OpenArkStudio/arknet/blob/main" + pathname;
    } else {
      const target = pathname.slice(6);
      if (target.endsWith(".md")) {
        if (!files.has(target)) throw new Error(`${file}: missing document ${target}`);
        destination = pageRoute(target);
      } else {
        destination = (image ? "/arknet/" : "/") + target;
      }
    }
    return destination + url.search + url.hash;
  }
  function walk(node) {
    if ((node.type === "link" || node.type === "image" || node.type === "definition") && node.url
        && !/^(?:[a-z][a-z\d+.-]*:|\/|#)/i.test(node.url)) {
      const destination = destinationFor(node.url, node.type === "image");
      const start = node.position.start.offset;
      const original = source.slice(start, node.position.end.offset);
      // Locate only the parsed destination; code blocks and labels stay byte-identical.
      const offset = original.lastIndexOf(node.url);
      if (offset < 0) throw new Error(`${file}: unsupported escaped link ${node.url}`);
      replacements.push({ start: start + offset, end: start + offset + node.url.length, value: destination });
    }
    if (node.type === "html") {
      // Nimbus prefixes component href props during rendering and AI export.
      // Inspect parsed HTML only: component examples inside code must stay intact.
      for (const tag of node.value.matchAll(/<LinkCard\b[^>]*>/g)) {
        const href = /\bhref=(['"])(.*?)\1/.exec(tag[0]);
        if (!href || /^(?:[a-z][a-z\d+.-]*:|\/|#)/i.test(href[2])) continue;
        const destination = destinationFor(href[2]);
        const start = node.position.start.offset + tag.index + href.index + href[0].indexOf(href[1]) + 1;
        replacements.push({ start, end: start + href[2].length, value: destination });
      }
    }
    for (const child of node.children ?? []) walk(child);
  }
  walk(tree);
  let body = source;
  for (const edit of replacements.sort((a, b) => b.start - a.start)) {
    body = body.slice(0, edit.start) + edit.value + body.slice(edit.end);
  }
  const names = components(tree);
  const imports = [];
  if (names.has("Aside")) imports.push('import { Aside } from "@/components/ui/aside";');
  const steps = ["Step", "Steps"].filter(name => names.has(name));
  if (steps.length) imports.push(`import { ${steps.join(", ")} } from "@/components/ui/steps";`);
  if (names.has("CardGrid")) imports.push('import { CardGrid } from "@/components/ui/card-grid";');
  if (names.has("LinkCard")) imports.push('import { LinkCard } from "@/components/ui/link-card";');
  const tabs = ["Tabs", "TabItem"].filter(name => names.has(name));
  if (tabs.length) imports.push(`import { ${tabs.join(", ")} } from "@/components/ui/tabs";`);
  return `---\ntitle: ${JSON.stringify(title)}\n---\n\n${imports.length ? imports.join("\n") + "\n\n" : ""}${body.trim()}\n`;
}
