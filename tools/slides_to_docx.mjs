/*
 * arabic-led-text-pipeline - slide deck to editable document
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Extracts the slide content out of the HTML deck and writes Markdown with one
 * slide per page, ready for pandoc to turn into a .docx.
 *
 * It reads the deck's own slide array rather than restating the content, so the
 * document cannot drift out of sync with the presentation. Edit the deck, re-run
 * this, and the document follows.
 *
 *   node tools/slides_to_docx.mjs > build/foundations-slides.md
 */

import fs from "node:fs";

const html = fs.readFileSync("docs/slides/foundations.html", "utf8");

/* The deck builds its 8x8 LED props by calling panel(); in a document those
   become a plain figure, so the pattern is rendered as text instead. */
function panel(pat) {
  return "<pre>" + pat.map(r =>
    [...r].map(c => (c === "#" ? "●" : c === "+" ? "◐" : "·")).join(" ")
  ).join("\n") + "</pre>";
}

const sectionsSrc = html.match(/const SECTIONS = (\[[^\]]*\]);/)[1];
const slidesSrc   = html.match(/const S = (\[[\s\S]*?\n\];)/)[1].replace(/;$/, "");

const SECTIONS = eval(sectionsSrc);
const S = eval(slidesSrc);

/* Minimal HTML to Markdown. The deck uses a small, known set of tags, so a
   general parser would be more machinery than the job needs. */
function toMd(h) {
  return h
    .replace(/<pre[^>]*>([\s\S]*?)<\/pre>/g, (_, code) =>
      "\n```\n" + code
        .replace(/<span class="[^"]*">/g, "").replace(/<\/span>/g, "")
        .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&amp;/g, "&")
        .replace(/^\n+|\n+$/g, "") + "\n```\n")
    .replace(/<table[^>]*>([\s\S]*?)<\/table>/g, (_, t) => {
      const rows = [...t.matchAll(/<tr>([\s\S]*?)<\/tr>/g)].map(m =>
        [...m[1].matchAll(/<t[hd]>([\s\S]*?)<\/t[hd]>/g)].map(c => clean(c[1])));
      if (!rows.length) return "";
      const width = Math.max(...rows.map(r => r.length));
      const pad = r => [...r, ...Array(width - r.length).fill("")];
      const head = pad(rows[0]);
      /*
       * Pandoc derives docx column widths from the length of the separator
       * row, so uniform "---" gives uniform columns and long cells wrap into
       * narrow ribbons. Sizing the dashes to the widest cell in each column
       * makes the rendered table match its content.
       */
      const widths = head.map((_, c) =>
        Math.max(3, ...rows.map(r => (pad(r)[c] || "").length)));
      const rule = widths.map(w => "-".repeat(Math.min(w, 40))).join("|");
      return "\n| " + head.join(" | ") + " |\n|" + rule + "|\n"
        + rows.slice(1).map(r => "| " + pad(r).join(" | ") + " |").join("\n") + "\n";
    })
    .replace(/<div class="card"><h3>(.*?)<\/h3>([\s\S]*?)<\/div>/g,
             (_, t, b) => `\n**${clean(t)}** — ${clean(b)}\n`)
    .replace(/<div class="key"[^>]*>([\s\S]*?)<\/div>/g, (_, b) => `\n> ${clean(b)}\n`)
    .replace(/<p class="small"[^>]*>([\s\S]*?)<\/p>/g, (_, b) => `\n*${clean(b)}*\n`)
    .replace(/<p class="lede"[^>]*>([\s\S]*?)<\/p>/g, (_, b) => `\n${clean(b)}\n`)
    .replace(/<p[^>]*>([\s\S]*?)<\/p>/g, (_, b) => `\n${clean(b)}\n`)
    .replace(/<div class="sub">([\s\S]*?)<\/div>/g, (_, b) => `\n*${clean(b)}*\n`)
    .replace(/<div class="by">([\s\S]*?)<\/div>/g, (_, b) => `\n${clean(b)}\n`)
    .replace(/<h1>([\s\S]*?)<\/h1>/g, (_, b) => `\n# ${clean(b)}\n`)
    .replace(/<\/?div[^>]*>/g, "")
    .replace(/\n{3,}/g, "\n\n")
    .trim();
}

function clean(s) {
  return s
    .replace(/<strong>(.*?)<\/strong>/g, "**$1**")
    .replace(/<em>(.*?)<\/em>/g, "**$1**")          /* accent colour -> bold */
    .replace(/<code>(.*?)<\/code>/g, "`$1`")
    .replace(/<[^>]+>/g, "")
    .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&amp;/g, "&")
    .replace(/\s+/g, " ")
    .trim();
}

const PAGE_BREAK =
  '\n```{=openxml}\n<w:p><w:r><w:br w:type="page"/></w:r></w:p>\n```\n';

const out = [];
let lastSection = -1;

S.forEach((s, i) => {
  if (i > 0) out.push(PAGE_BREAK);

  if (s.sect !== lastSection) {
    out.push(`\n## ${SECTIONS[s.sect]}\n`);
    lastSection = s.sect;
  }

  const n = String(i + 1).padStart(2, "0");
  if (s.title) {
    out.push(toMd(s.html));
  } else {
    out.push(`\n### ${n} · ${s.h}\n`);
    out.push(toMd(s.html));
  }
});

const md = out.join("\n") + "\n";

/*
 * Verify nothing was dropped.
 *
 * The first version matched `<table>` literally, so one table written as
 * `<table style="...">` vanished from the document with no error at all -
 * content loss that looked like success. Counting structures on both sides
 * turns that class of bug into a failure instead of a silent omission.
 */
/* Count against the EVALUATED slides, not the raw file: panel() builds its
   <pre> blocks at eval time, so they do not appear in the source text. */
const countIn = (re) =>
  S.reduce((n, s) => n + ((s.html || "").match(re) || []).length, 0);
const checks = [
  /* Matches any pipe-table separator row, not one fixed dash width - the
     widths are now proportional to content. */
  ["tables", countIn(/<table[^>]*>/g),
             (md.match(/^\|[-|]+\|$/gm) || []).length],
  /* Page breaks are raw-openxml blocks whose opening fence carries an
     attribute, so only their CLOSING fence looks like a plain ```. Subtract
     them before pairing, or every page break counts as half a code block. */
  ["code blocks", countIn(/<pre[^>]*>/g),
             ((md.match(/^```$/gm) || []).length
              - (md.match(/^```\{=openxml\}$/gm) || []).length) / 2],
  ["slides", S.length,
             (md.match(/^### /gm) || []).length + 1],
];
let bad = false;
for (const [what, want, got] of checks) {
  if (want !== got) { console.error(`MISMATCH ${what}: deck has ${want}, document has ${got}`); bad = true; }
}
if (bad) process.exit(1);
console.error(`ok — ${S.length} slides, ${checks[0][1]} tables, ${checks[1][1]} code blocks`);

process.stdout.write(md);
