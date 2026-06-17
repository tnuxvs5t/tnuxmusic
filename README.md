# tnuxmusic

一个基于 Qt 6 的桌面音乐平台 + 播放器。

## 技术栈

- Qt 6.11.1 / Qt Quick Controls 2：界面；
- Qt Multimedia：本地播放器；
- C++23：曲库、播放、歌词、脚本桥；
- OpenSSL libcrypto：NCM AES 解密；
- QJSEngine：用 JavaScript 整理专辑和动态管理曲库；
- `.tly`：自定义动态歌词格式，支持时间轴、滚动、翻译和标签。

## 当前功能

- 内置示例 Techno 专辑：`Physics` / `nova9tekgrid`；
- 扫描本地音乐文件夹；
- 自动识别常见音频格式：`mp3/flac/wav/m4a/aac/ogg/opus/...`；
- 自动关联同名 `.tly` 歌词；
- 自动关联同目录 `cover/folder/front/album/artwork` 封面；
- 一首歌可挂多个音质；
- 曲库 JSON 导入、导出、合并；
- 当前曲库本地化导出为标准 ZIP（含 `library.json`、音频、封面、歌词）；
- JS 曲库整理脚本；
- 播放、暂停、进度、音量；
- `.tly` 动态歌词滚动和翻译显示。
- 播放队列、上一首 / 下一首、自动续播；
- 本地歌单保存、加载、追加、删除；
- 专辑墙与专辑详情；
- 专辑从曲库删除（只删曲库记录，不删磁盘文件）；
- MP3 ID3v2 / FLAC Vorbis Comment 基础 tag 读取；
- `.tly` 逐字时间轴高亮。
- 扫描音乐时自动把同名 `.lrc` 转成 `.tly`；
- 扫描时识别并解密 `.ncm`，自动生成普通音频文件和内嵌封面后纳入曲库。

## 构建

```bash
cmake --preset qt6-debug
cmake --build --preset qt6-debug
./build/qt6-debug/tnuxmusic
```

首次运行时，如果本机还没有默认曲库，应用会自动把仓库里的示例专辑：

```text
Physics - nova9tekgrid/
```

扫描进默认曲库。

Qt 路径默认使用：

```text
/home/tnuzy/Qt/6.11.1/gcc_64
```

## 文档

- 曲库格式：[`docs/LIBRARY_SCHEMA.md`](docs/LIBRARY_SCHEMA.md)
- TLY 歌词格式：[`docs/TLY_FORMAT.md`](docs/TLY_FORMAT.md)
- JS 示例：[`scripts/normalize_album.js`](scripts/normalize_album.js)
- LRC 转 TLY：[`scripts/lrc_to_tly.js`](scripts/lrc_to_tly.js)
- 合并导入曲库：[`scripts/merge_import_library.js`](scripts/merge_import_library.js)
- TLY 示例：[`examples/demo.tly`](examples/demo.tly)

## JS 处理本地 LRC

```bash
node scripts/lrc_to_tly.js "/path/to/song.mp3" --album "Desktop LRC Test" --genre Rap --write
node scripts/merge_import_library.js "/path/to/song.tnux.import.json" "$HOME/.local/share/tnux/tnuxmusic/library.json"
```

第一个脚本会把同名 `.lrc` 转成 `.tly`，同时生成一个可导入的曲库 JSON。

`lrc_to_tly.js` 会优先读取 MP3 的 ID3v2：

- `TIT2` 标题；
- `TPE1` 艺术家；
- `TALB` 专辑名；
- `TCON` 风格；
- `APIC` 内嵌封面，并导出为同名 `.cover.jpg/.png`。

也可以传入翻译 JSON 生成 TLY 翻译行：

```bash
node scripts/lrc_to_tly.js "/path/to/song.mp3" --translation-json "/path/to/song.zh-CN.json" --write
```

生成的 `.tly` 会包含：

```tly
[00:12.000]原文
[00:12.000|tr=zh-CN]中文翻译
```

## 扫描时 LRC / NCM 行为

应用内置扫描器会：

- 优先使用同名 `.tly`；
- 如果没有 `.tly` 但有同名 `.lrc`，自动生成 `.tly`；
- 如果旁边存在同名 `.zh-CN.json` 翻译数组，也会写入 `tr=zh-CN` 行；
- 发现 `.ncm` 会调用内置转换器；
- 转换器会在 `.ncm` 同目录生成同名 `.mp3` / `.flac` / `.m4a` 等普通音频文件；
- 如果 `.ncm` 带内嵌封面，会生成同名 `.cover.jpg` / `.cover.png` 等封面 sidecar；
- 扫描器会继续把输出音频纳入曲库。

## 曲库本地化 ZIP

顶部“本地化ZIP”会把当前曲库打包为标准 ZIP：

- `library.json`：包内相对路径版曲库索引；
- `music/...`：当前曲库引用到的音频、封面、歌词资源；
- ZIP 使用标准 store 条目，方便系统解压工具直接读取。

NCM 转换入口在：

```text
src/NcmImportService.h
src/NcmImportService.cpp
```

核心函数：

```cpp
NcmImportResult NcmImportService::convertToOpenAudio(const QString &inputPath)
```

成功时返回 `Status::Converted`，并把 `outputAudioPath` 设为生成的普通音频文件路径。

## TLY 逐字高亮

行内可以使用绝对或相对时间标记：

```tly
[00:12.000]<00:12.000>逐<00:12.250>字<00:12.520>高亮
[00:16.000]<+00:00.000>相<+00:00.180>对<+00:00.360>时间
```

相对时间以当前歌词行开始时间为基准。
