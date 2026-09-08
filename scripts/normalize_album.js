// 更多操作 → 运行整理脚本。只整理空白，不猜测录音身份或合并曲目。
// ID、originKeys、专辑分组、每个音质和其他字段都保留。
function norm(s) { return String(s || "").trim().replace(/\s+/g, " "); }
function organize(library) {
    for (const track of library.tracks || []) {
        for (const field of ["title", "artist", "album", "albumArtist", "genre"])
            if (typeof track[field] === "string") track[field] = norm(track[field]);
    }
    return library;
}
