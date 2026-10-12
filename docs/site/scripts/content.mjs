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

export function convertPage(source, file, files) {
  const tree = parser.parse(source);
  const heading = tree.children.find((node) => node.type === "heading" && node.depth === 1);
  if (!heading) throw new Error(`${file}: missing page heading`);
  const title = text(heading);
  const replacements = [{ start: heading.position.start.offset, end: heading.position.end.offset, value: "" }];
  function walk(node) {
    if ((node.type === "link" || node.type === "image" || node.type === "definition") && node.url
        && !/^(?:[a-z][a-z\d+.-]*:|\/|#)/i.test(node.url)) {
      const url = new URL(node.url, `https://source.invalid/docs/${file}`);
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
          // Nimbus prefixes links, but leaves image sources unchanged.
          destination = (node.type === "image" ? "/arknet/" : "/") + target;
        }
      }
      destination += url.search + url.hash;
      const start = node.position.start.offset;
      const original = source.slice(start, node.position.end.offset);
      // Locate only the parsed destination; code blocks and labels stay byte-identical.
      const offset = original.lastIndexOf(node.url);
      if (offset < 0) throw new Error(`${file}: unsupported escaped link ${node.url}`);
      replacements.push({ start: start + offset, end: start + offset + node.url.length, value: destination });
    }
    for (const child of node.children ?? []) walk(child);
  }
  walk(tree);
  let body = source;
  for (const edit of replacements.sort((a, b) => b.start - a.start)) {
    body = body.slice(0, edit.start) + edit.value + body.slice(edit.end);
  }
  return `---\ntitle: ${JSON.stringify(title)}\n---\n\n${body.trim()}\n`;
}
