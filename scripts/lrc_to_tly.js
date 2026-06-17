#!/usr/bin/env node
// Convert a local audio + .lrc pair into tnuxmusic .tly and an importable library JSON.
//
// Examples:
//   node scripts/lrc_to_tly.js "/path/song.mp3"
//   node scripts/lrc_to_tly.js "/path/song.mp3" --write
//   node scripts/lrc_to_tly.js "/path/song.mp3" --album "Desktop Imports" --genre Rap --write
//   node scripts/lrc_to_tly.js "/path/song.mp3" --translation-json song.zh-CN.json --write

const fs = require("fs");
const path = require("path");
const crypto = require("crypto");

function usage() {
  console.log(`Usage: node scripts/lrc_to_tly.js <audio> [options]

Options:
  --lrc <path>          LRC path. Default: same basename as audio.
  --out <path>          TLY output path. Default: same basename as audio + .tly.
  --library-out <path>  Import JSON output path. Default: same basename + .tnux.import.json.
  --cover-out <path>    Extract embedded MP3 cover here. Default: same basename + .cover.<ext>.
  --translation-json <path>
                        JSON array/object with translated lyric lines.
  --translation-lang <tag>
                        Translation tag for TLY. Default: zh-CN.
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

function synchsafe32(buf, off) {
  return ((buf[off] & 0x7f) << 21) | ((buf[off + 1] & 0x7f) << 14) |
    ((buf[off + 2] & 0x7f) << 7) | (buf[off + 3] & 0x7f);
}

function decodeUtf16BE(buf) {
  const swapped = Buffer.alloc(buf.length);
  for (let i = 0; i + 1 < buf.length; i += 2) {
    swapped[i] = buf[i + 1];
    swapped[i + 1] = buf[i];
  }
  return swapped.toString("utf16le");
}

function decodeTextPayload(payload) {
  if (!payload || payload.length === 0) return "";
  const enc = payload[0];
  let body = payload.subarray(1);
  let text;
  if (enc === 0) {
    text = body.toString("latin1");
  } else if (enc === 1) {
    if (body.length >= 2 && body[0] === 0xfe && body[1] === 0xff) {
      text = decodeUtf16BE(body.subarray(2));
    } else if (body.length >= 2 && body[0] === 0xff && body[1] === 0xfe) {
      text = body.subarray(2).toString("utf16le");
    } else {
      text = body.toString("utf16le");
    }
  } else if (enc === 2) {
    text = decodeUtf16BE(body);
  } else {
    text = body.toString("utf8");
  }
  return text.replace(/\0/g, " ").trim().replace(/\s+/g, " ");
}

function findTerminator(buf, pos, enc) {
  if (enc === 1 || enc === 2) {
    for (let i = pos; i + 1 < buf.length; i += 2) {
      if (buf[i] === 0 && buf[i + 1] === 0) return i;
    }
    return -1;
  }
  return buf.indexOf(0, pos);
}

function parseApic(payload) {
  if (!payload || payload.length < 8) return null;
  const enc = payload[0];
  let pos = 1;
  const mimeEnd = payload.indexOf(0, pos);
  if (mimeEnd < 0) return null;
  const mime = payload.subarray(pos, mimeEnd).toString("latin1").toLowerCase();
  pos = mimeEnd + 1;
  const pictureType = payload[pos++];
  const descEnd = findTerminator(payload, pos, enc);
  if (descEnd < 0) return null;
  pos = descEnd + ((enc === 1 || enc === 2) ? 2 : 1);
  const data = payload.subarray(pos);
  if (!data.length) return null;

  let ext = "bin";
  if (mime.includes("jpeg") || mime.includes("jpg")) ext = "jpg";
  else if (mime.includes("png")) ext = "png";
  else if (mime.includes("webp")) ext = "webp";

  return { mime, ext, pictureType, data };
}

function parseId3v2(audioPath) {
  const buf = fs.readFileSync(audioPath);
  if (buf.length < 10 || buf.subarray(0, 3).toString("latin1") !== "ID3") {
    return { tags: {}, cover: null };
  }

  const major = buf[3];
  if (major < 3 || major > 4) return { tags: {}, cover: null };

  const tagSize = synchsafe32(buf, 6);
  const end = Math.min(buf.length, 10 + tagSize);
  let pos = 10;
  const tags = {};
  let cover = null;

  const fieldMap = {
    TIT2: "title",
    TPE1: "artist",
    TALB: "album",
    TCON: "genre",
    TRCK: "track",
    TPOS: "disc",
    TDRC: "year",
    TYER: "year"
  };

  while (pos + 10 <= end) {
    const id = buf.subarray(pos, pos + 4).toString("latin1");
    if (!/^[A-Z0-9]{4}$/.test(id)) break;
    const size = major === 4 ? synchsafe32(buf, pos + 4) : buf.readUInt32BE(pos + 4);
    pos += 10;
    if (size <= 0 || pos + size > end) break;
    const payload = buf.subarray(pos, pos + size);
    pos += size;

    if (fieldMap[id]) {
      const value = decodeTextPayload(payload);
      if (value && !tags[fieldMap[id]]) tags[fieldMap[id]] = value;
    } else if (id === "APIC" && !cover) {
      cover = parseApic(payload);
    }
  }

  return { tags, cover };
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

function loadTranslations(filePath, rows) {
  if (!filePath) return new Map();
  const raw = JSON.parse(fs.readFileSync(path.resolve(filePath), "utf8"));
  const out = new Map();

  if (Array.isArray(raw)) {
    let k = 0;
    for (const row of rows) {
      if (!row.text) continue;
      if (raw[k]) out.set(row.startMs, String(raw[k]).trim());
      k++;
    }
    return out;
  }

  for (const [key, value] of Object.entries(raw)) {
    let ms;
    if (/^\d+$/.test(key)) ms = Number(key);
    else ms = parseTimeMs(key);
    if (value) out.set(ms, String(value).trim());
  }
  return out;
}

function escapeTlyText(s) {
  return String(s || "").replace(/\r?\n/g, " ").trim();
}

function toTly({ meta, rows, artist, title, album, genre, translations, translationLang }) {
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
    const tr = translations.get(row.startMs);
    if (tr) out.push(`[${formatTime(row.startMs)}|tr=${translationLang}]${escapeTlyText(tr)}`);
  }
  out.push("");
  return out.join("\n");
}

function stableId({ artist, album, title, audio }) {
  const key = `${artist}\u0001${album}\u0001${title}\n${audio}`;
  return crypto.createHash("sha1").update(key).digest("hex").slice(0, 16);
}

function libraryJson({ audio, tlyOut, coverOut, artist, title, album, genre }) {
  const ext = path.extname(audio).slice(1).toUpperCase();
  const track = {
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
  };
  if (coverOut) track.cover = coverOut;

  return {
    schema: "tnuxmusic.library.v1",
    app: "tnuxmusic",
    version: 1,
    tracks: [track]
  };
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  if (!fs.existsSync(args.audio)) throw new Error(`Audio not found: ${args.audio}`);

  const defaultLrc = path.join(path.dirname(args.audio), path.basename(args.audio, path.extname(args.audio)) + ".lrc");
  const lrcPath = path.resolve(args.lrc || defaultLrc);
  if (!fs.existsSync(lrcPath)) throw new Error(`LRC not found: ${lrcPath}`);

  const inferred = inferFromFilename(args.audio);
  const id3 = parseId3v2(args.audio);
  const parsed = parseLrc(fs.readFileSync(lrcPath, "utf8"));
  const artist = args.artist || id3.tags.artist || parsed.meta.ar || inferred.artist || "Unknown Artist";
  const title = args.title || id3.tags.title || parsed.meta.ti || inferred.title || "Untitled";
  const album = args.album || id3.tags.album || parsed.meta.al || "Desktop LRC Imports";
  const genre = args.genre || id3.tags.genre || "";
  const translations = loadTranslations(args.translationJson, parsed.rows);
  const translationLang = args.translationLang || "zh-CN";

  const tlyOut = path.resolve(args.out || path.join(path.dirname(args.audio), path.basename(args.audio, path.extname(args.audio)) + ".tly"));
  const libraryOut = path.resolve(args.libraryOut || path.join(path.dirname(args.audio), path.basename(args.audio, path.extname(args.audio)) + ".tnux.import.json"));
  const coverOut = id3.cover
    ? path.resolve(args.coverOut || path.join(path.dirname(args.audio), path.basename(args.audio, path.extname(args.audio)) + `.cover.${id3.cover.ext}`))
    : "";
  const tly = toTly({ meta: parsed.meta, rows: parsed.rows, artist, title, album, genre, translations, translationLang });
  const lib = libraryJson({ audio: args.audio, tlyOut, coverOut, artist, title, album, genre });

  if (args.write) {
    fs.writeFileSync(tlyOut, tly, "utf8");
    if (id3.cover && coverOut) fs.writeFileSync(coverOut, id3.cover.data);
    fs.writeFileSync(libraryOut, JSON.stringify(lib, null, 2) + "\n", "utf8");
  }

  console.log(JSON.stringify({
    audio: args.audio,
    lrc: lrcPath,
    tlyOut,
    libraryOut,
    write: args.write,
    id3: id3.tags,
    embeddedCover: id3.cover ? { mime: id3.cover.mime, bytes: id3.cover.data.length, coverOut } : null,
    translationLines: translations.size,
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
