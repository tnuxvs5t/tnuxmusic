#!/usr/bin/env node
// Convert a local audio + .lrc pair into tnuxmusic .tly and an importable library JSON.
//
// Examples:
//   node scripts/lrc_to_tly.js "/path/song.mp3"
//   node scripts/lrc_to_tly.js "/path/song.mp3" --write
//   node scripts/lrc_to_tly.js "/path/song.mp3" --album "Desktop Imports" --genre Rap --write

const fs = require("fs");
const path = require("path");
const crypto = require("crypto");

function usage() {
  console.log(`Usage: node scripts/lrc_to_tly.js <audio> [options]

Options:
  --lrc <path>          LRC path. Default: same basename as audio.
  --out <path>          TLY output path. Default: same basename as audio + .tly.
  --library-out <path>  Import JSON output path. Default: same basename + .tnux.import.json.
  --artist <name>       Override artist.
  --title <name>        Override title.
  --album <name>        Override album.
  --genre <name>        Override genre.
  --write               Write .tly and import JSON. Without this, only preview is printed.
  --dump-tly            Print full generated TLY to stdout.
`);
}

function parseArgs(argv) {
  const args = { write: false, dumpTly: false };
  const rest = [];
  for (let i = 0; i < argv.length; ++i) {
    const a = argv[i];
    if (a === "--write") args.write = true;
    else if (a === "--dump-tly") args.dumpTly = true;
    else if (a.startsWith("--")) {
      const key = a.slice(2).replace(/-([a-z])/g, (_, c) => c.toUpperCase());
      if (i + 1 >= argv.length) throw new Error(`Missing value for ${a}`);
      args[key] = argv[++i];
    } else {
      rest.push(a);
    }
  }
  if (rest.length !== 1) {
    usage();
    process.exit(rest.length === 0 ? 0 : 1);
  }
  args.audio = path.resolve(rest[0]);
  return args;
}

function inferFromFilename(audioPath) {
  const base = path.basename(audioPath, path.extname(audioPath));
  const m = base.match(/^(.+?)\s+-\s+(.+)$/);
  if (!m) return { artist: "", title: base };
  return { artist: m[1].trim(), title: m[2].trim() };
}

function parseTimeMs(s) {
  const parts = s.trim().replace(",", ".").split(":");
  let sec = Number(parts.pop());
  if (!Number.isFinite(sec)) throw new Error(`Bad LRC time: ${s}`);
  let total = Math.floor(sec) * 1000 + Math.round((sec - Math.floor(sec)) * 1000);
  let mul = 60 * 1000;
  while (parts.length) {
    const v = Number(parts.pop());
    if (!Number.isFinite(v)) throw new Error(`Bad LRC time: ${s}`);
    total += v * mul;
    mul *= 60;
  }
  return total;
}

function formatTime(ms) {
  ms = Math.max(0, Math.round(ms));
  const h = Math.floor(ms / 3600000);
  ms %= 3600000;
  const m = Math.floor(ms / 60000);
  ms %= 60000;
  const s = Math.floor(ms / 1000);
  const z = ms % 1000;
  const pad2 = n => String(n).padStart(2, "0");
  const pad3 = n => String(n).padStart(3, "0");
  return h > 0 ? `${h}:${pad2(m)}:${pad2(s)}.${pad3(z)}` : `${pad2(m)}:${pad2(s)}.${pad3(z)}`;
}

function parseLrc(text) {
  const meta = {};
  const rows = [];
  const metaRe = /^\[([a-zA-Z]+):([^\]]*)\]\s*$/;
  const timeRe = /\[(\d{1,3}:\d{2}(?:[.,:]\d{1,3})?)\]/g;

  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line) continue;

    const mm = line.match(metaRe);
    if (mm) {
      meta[mm[1].toLowerCase()] = mm[2].trim();
      continue;
    }

    const times = [];
    let m;
    while ((m = timeRe.exec(line)) !== null) {
      times.push(parseTimeMs(m[1]));
    }
    if (!times.length) continue;

    const textPart = line.replace(timeRe, "").trim();
    for (const t of times) {
      rows.push({ startMs: t, text: textPart });
    }
  }

  rows.sort((a, b) => a.startMs - b.startMs || a.text.localeCompare(b.text));
  return { meta, rows };
}

function escapeTlyText(s) {
  return String(s || "").replace(/\r?\n/g, " ").trim();
}

function toTly({ meta, rows, artist, title, album, genre }) {
  const out = [];
  out.push(`@title = ${title}`);
  out.push(`@artist = ${artist}`);
  if (album) out.push(`@album = ${album}`);
  if (genre) out.push(`@genre = ${genre}`);
  if (meta.by) out.push(`@lrc_by = ${meta.by}`);
  out.push("@source = lrc");
  out.push("");
  for (const row of rows) {
    if (!row.text) continue;
    out.push(`[${formatTime(row.startMs)}]${escapeTlyText(row.text)}`);
  }
  out.push("");
  return out.join("\n");
}

function stableId({ artist, album, title, audio }) {
  const key = `${artist}\u0001${album}\u0001${title}\n${audio}`;
  return crypto.createHash("sha1").update(key).digest("hex").slice(0, 16);
}

function libraryJson({ audio, tlyOut, artist, title, album, genre }) {
  const ext = path.extname(audio).slice(1).toUpperCase();
  return {
    schema: "tnuxmusic.library.v1",
    app: "tnuxmusic",
    version: 1,
    tracks: [
      {
        id: stableId({ artist, album, title, audio }),
        title,
        artist,
        album,
        genre,
        lyrics: tlyOut,
        qualities: [
          {
            label: ext === "MP3" ? "MP3 320k" : ext,
            path: audio,
            codec: ext
          }
        ]
      }
    ]
  };
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  if (!fs.existsSync(args.audio)) throw new Error(`Audio not found: ${args.audio}`);

  const defaultLrc = path.join(path.dirname(args.audio), path.basename(args.audio, path.extname(args.audio)) + ".lrc");
  const lrcPath = path.resolve(args.lrc || defaultLrc);
  if (!fs.existsSync(lrcPath)) throw new Error(`LRC not found: ${lrcPath}`);

  const inferred = inferFromFilename(args.audio);
  const parsed = parseLrc(fs.readFileSync(lrcPath, "utf8"));
  const artist = args.artist || parsed.meta.ar || inferred.artist || "Unknown Artist";
  const title = args.title || parsed.meta.ti || inferred.title || "Untitled";
  const album = args.album || parsed.meta.al || "Desktop LRC Imports";
  const genre = args.genre || "";

  const tlyOut = path.resolve(args.out || path.join(path.dirname(args.audio), path.basename(args.audio, path.extname(args.audio)) + ".tly"));
  const libraryOut = path.resolve(args.libraryOut || path.join(path.dirname(args.audio), path.basename(args.audio, path.extname(args.audio)) + ".tnux.import.json"));
  const tly = toTly({ meta: parsed.meta, rows: parsed.rows, artist, title, album, genre });
  const lib = libraryJson({ audio: args.audio, tlyOut, artist, title, album, genre });

  if (args.write) {
    fs.writeFileSync(tlyOut, tly, "utf8");
    fs.writeFileSync(libraryOut, JSON.stringify(lib, null, 2) + "\n", "utf8");
  }

  console.log(JSON.stringify({
    audio: args.audio,
    lrc: lrcPath,
    tlyOut,
    libraryOut,
    write: args.write,
    artist,
    title,
    album,
    genre,
    lyricLines: parsed.rows.filter(r => r.text).length,
    firstLyric: parsed.rows.find(r => r.text) || null,
    importTrackId: lib.tracks[0].id
  }, null, 2));

  if (args.dumpTly) {
    console.log("\n--- TLY ---");
    console.log(tly);
  }
}

try {
  main();
} catch (e) {
  console.error(e.stack || e.message);
  process.exit(1);
}

