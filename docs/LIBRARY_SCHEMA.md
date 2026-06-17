# tnuxmusic 曲库 JSON v1

曲库用于导入、导出、合并，也会交给 JS 脚本整理。

```json
{
  "schema": "tnuxmusic.library.v1",
  "app": "tnuxmusic",
  "version": 1,
  "tracks": [
    {
      "id": "stable-id",
      "title": "Song",
      "artist": "Artist",
      "album": "Album",
      "genre": "Pop",
      "year": 2026,
      "disc": 1,
      "track": 1,
      "cover": "/music/Artist/Album/cover.jpg",
      "lyrics": "/music/Artist/Album/Song.tly",
      "qualities": [
        {
          "label": "Lossless",
          "path": "/music/Artist/Album/Song.flac",
          "codec": "FLAC",
          "bitrate": 0,
          "sampleRate": 0
        }
      ]
    }
  ]
}
```

合并规则：

- `artist + album + title` 相同的条目会合并为一首歌；
- 不同音质写入同一个 `qualities` 数组；
- 缺失的封面、歌词、年份、风格会从被合并条目补齐；
- 相同文件路径的音质不会重复加入。

JS 脚本规则：

- 应用会提供全局变量 `library`；
- 脚本可以直接修改 `library`；
- 或定义 `function organize(library) { return library }`；
- 返回值必须仍是上面的曲库对象。

## tag 读取

扫描曲库时当前会读取：

- MP3：ID3v2 `TIT2/TPE1/TALB/TCON/TRCK/TPOS/TDRC/TYER`
- FLAC：Vorbis Comment `TITLE/ARTIST/ALBUM/GENRE/TRACKNUMBER/DISCNUMBER/DATE/YEAR`

若 tag 缺失，则回退到目录结构：

```text
Artist/Album/Song.ext
```

歌单保存在应用数据目录的 `playlists.json`，内部使用曲目的稳定 `id` 引用曲库。
