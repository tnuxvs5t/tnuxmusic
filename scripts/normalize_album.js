// tnuxmusic 曲库整理脚本示例。
// 运行方式：应用内点击“JS整理”，选择这个文件。
//
// 输入：全局变量 library，结构见 docs/LIBRARY_SCHEMA.md。
// 输出：返回整理后的 library，或直接修改全局 library。

function norm(s) {
  return String(s || "").trim().replace(/\s+/g, " ");
}

function keyOf(track) {
  return [
    norm(track.artist).toLowerCase(),
    norm(track.album).toLowerCase(),
    norm(track.title).toLowerCase(),
  ].join("\u0001");
}

function organize(library) {
  const byKey = new Map();
  const out = [];

  for (const raw of library.tracks || []) {
    const t = Object.assign({}, raw);
    t.title = norm(t.title) || "Untitled";
    t.artist = norm(t.artist) || "Unknown Artist";
    t.album = norm(t.album) || "Unknown Album";
    t.qualities = Array.isArray(t.qualities) ? t.qualities : [];

    const key = keyOf(t);
    if (!byKey.has(key)) {
      byKey.set(key, t);
      out.push(t);
    } else {
      const kept = byKey.get(key);
      const seen = new Set((kept.qualities || []).map(q => q.path));
      for (const q of t.qualities) {
        if (q && q.path && !seen.has(q.path)) {
          kept.qualities.push(q);
          seen.add(q.path);
        }
      }
      if (!kept.cover && t.cover) kept.cover = t.cover;
      if (!kept.lyrics && t.lyrics) kept.lyrics = t.lyrics;
    }
  }

  out.sort((a, b) =>
    norm(a.artist).localeCompare(norm(b.artist)) ||
    norm(a.album).localeCompare(norm(b.album)) ||
    (Number(a.disc || 1) - Number(b.disc || 1)) ||
    (Number(a.track || 0) - Number(b.track || 0)) ||
    norm(a.title).localeCompare(norm(b.title))
  );

  library.tracks = out;
  return library;
}

