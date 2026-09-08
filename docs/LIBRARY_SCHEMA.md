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
      "albumArtist": "Album Artist",
      "albumId": "optional-manual-group-id",
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

身份与合并规则：

- `id` 是曲目和歌单引用的稳定标识；手动修改、移动、拆分专辑不改变它。`artist` 是演唱者，`albumArtist` 是专辑艺术家，两者独立。
- `albumId` 是显式手动分组，优先于专辑标签和目录规则。相同 `albumId` 的曲目属于同一张专辑；外部 JSON 应保证该组专辑名称、专辑艺术家和年份一致。
- 新保存的曲目包含 `originKeys` 字符串数组，记录整理前的录音元数据身份。旧索引加载时根据原始标签生成；手动整理保留该字段。它不是文件哈希或外部可信身份证明。
- 合并优先匹配已有音频路径；同 ID 且具有共同 `originKeys` 时匹配同源曲目，即使手动改过专辑名称或资源换了目录，也保留现有 ID 和手动归属。
- 无上述匹配时，仅在 `artist + album + title + disc + track + year` 唯一且手动分组兼容时合并。多个已有曲目共享该键时不擅自选一个；不同录音使用相同外部 ID 时保留为独立条目并分配新 ID。
- 同路径音质不重复加入，不同路径保留；目前没有跨包音频内容哈希去重。完全相同的元数据仍不足以证明同一录音，严格区分的曲目建议使用不同 ID 和手动专辑分组。
- 只补充空缺的封面、歌词等信息。曲库 JSON 的相对路径以 JSON 所在目录为基准。无效索引、重复显式 ID 或重复生成 ID 的替换操作不会发布。

扫描、导入、合并、标签补全统一准备副本，原子保存成功后才发布模型；失败或取消保持原索引和前台模型。后台任务提交完成后支持本进程内一步撤销，初始加载不建立撤销快照。

本地化 ZIP 只支持本应用输出的 store ZIP，不支持 deflate、ZIP64、加密或数据描述符。导入按引用解包资源并验证 CRC；合并对引用的已有缓存也读取校验，损坏的缓存会重新解包。未引用缓存暂不自动回收。

JS 脚本规则：

- 应用会提供全局变量 `library`；
- 脚本可以直接修改 `library`；
- 或定义 `function organize(library) { return library }`；
- 返回值必须仍是上面的曲库对象。

## tag 读取

扫描曲库时当前会读取：

- MP3：ID3v2 `TIT2/TPE1/TPE2/TALB/TCON/TRCK/TPOS/TDRC/TYER`
- FLAC：Vorbis Comment `TITLE/ARTIST/ALBUMARTIST/ALBUM/GENRE/TRACKNUMBER/DISCNUMBER/DATE/YEAR`

若 tag 缺失，则回退到目录结构：

```text
Artist/Album/Song.ext
```

歌单保存在应用数据目录的 `playlists.json`，内部使用曲目的稳定 `id` 引用曲库。

`normalize_album.js` 仅规范文本空白，保留曲目、ID、originKeys、手动分组和音质；不会按同名标题折叠曲目。命令行合并脚本调用原生应用后端，拒绝在应用占用目标数据目录时写入。
