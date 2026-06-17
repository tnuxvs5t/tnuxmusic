# tnuxmusic

tnuxmusic 是一个基于 Qt 6 的桌面音乐平台 + 播放器，面向本地曲库、网易云 `.ncm` 曲库迁移、歌词整理、专辑归档和曲库交流。

---

# 上片：快速介绍

## 它解决什么问题

tnuxmusic 的目标不是做一个臃肿的在线客户端，而是把你的音乐文件真正变成本地、开放、可整理、可迁移的曲库：

- 扫描本地音乐文件夹；
- 自动解密 `.ncm`，生成普通 `mp3/flac/m4a/...` 音频；
- 自动导出 `.ncm` 内嵌封面为同名 `.cover.jpg/.png`；
- 自动识别封面、歌词、专辑、音质；
- 支持 `.lrc` 自动转 `.tly` 动态歌词；
- 支持曲库 JSON 导入、导出、合并；
- 支持把当前曲库整体打包成标准 ZIP，方便上传、备份、交流和迁移。

## 核心优势

- **高性能、低占用**
  C++23 + Qt 原生实现，启动和运行开销低，适合长期作为本地音乐库使用。

- **避开网易云客户端内存泄漏/臃肿问题**
  不依赖官方客户端常驻后台，不需要加载复杂在线页面和广告组件。

- **跨平台，无须 Wine**
  基于 Qt 6，目标是 Linux / Windows / macOS 都能原生运行；在 Linux 上不需要 Wine 跑 Windows 版网易云。

- **无缝接入网易云曲库资产**
  扫描 `.ncm` 时自动解密为开放音频格式，并提取内嵌封面，后续按普通本地音乐处理。

- **曲库可导出、可上传、可交流、可合并**
  曲库可以导出为 JSON，也可以本地化打包为标准 ZIP。别人拿到后可以导入或合并，不再被某个客户端锁死。

- **歌词和专辑整理友好**
  支持 `.tly` 动态歌词、`.lrc` 自动转换、翻译行、逐字时间轴、专辑墙、多音质合并。

## 快速使用

### 1. 构建运行

```bash
cmake --preset qt6-debug
cmake --build --preset qt6-debug
./build/qt6-debug/tnuxmusic
```

Qt 路径默认使用：

```text
/home/tnuzy/Qt/6.11.1/gcc_64
```

首次运行时，如果本机还没有默认曲库，应用会自动把仓库里的示例专辑扫描进默认曲库：

```text
Physics - nova9tekgrid/
```

### 2. 扫描音乐

点击顶部 **扫描音乐**，选择包含音频、歌词、封面或 `.ncm` 文件的目录。

扫描器会自动处理：

- `mp3/flac/wav/m4a/aac/ogg/opus/...`；
- `.ncm` 解密；
- 同名 `.tly`；
- 同名 `.lrc` 自动转 `.tly`；
- 同名 `.zh-CN.json` 翻译数组；
- 同名封面和专辑封面。

### 3. 导入 / 导出 / 合并曲库

顶部按钮：

- **导入**：加载一个曲库 JSON，替换当前曲库；
- **合并**：把另一个曲库 JSON 合并进当前曲库；
- **导出**：导出当前曲库 JSON；
- **本地化ZIP**：把当前曲库的索引、音频、封面、歌词打包成标准 ZIP。

## 为什么推荐

如果你的音乐主要来自本地文件、网易云下载目录、朋友分享的曲库包，tnuxmusic 更适合做“长期曲库底座”：

- 文件变成开放格式，不再被 `.ncm` 锁住；
- 曲库索引是普通 JSON，便于版本管理、脚本处理、合并和分享；
- ZIP 本地化包是标准压缩包，脱离应用也能解压查看；
- 播放、歌词、封面、专辑墙、队列和歌单都围绕本地曲库工作；
- 后续可以用 JS 脚本继续批量整理元数据。

---

# 下片：详细技术内容和扩展指南

## 技术栈

- Qt 6.11.1 / Qt Quick Controls 2：界面；
- Qt Multimedia：本地播放器；
- C++23：曲库、播放、歌词、脚本桥；
- OpenSSL libcrypto：NCM AES 解密；
- QJSEngine：用 JavaScript 整理专辑和动态管理曲库；
- `.tly`：自定义动态歌词格式，支持时间轴、滚动、翻译和标签。

## 当前功能清单

- 内置示例 Techno 专辑：`Physics` / `nova9tekgrid`；
- 扫描本地音乐文件夹；
- 自动识别常见音频格式；
- 自动关联同名 `.tly` 歌词；
- 自动关联同名 `.lrc` 并转换成 `.tly`；
- 自动关联同目录 `cover/folder/front/album/artwork` 封面；
- 自动识别同名 `.cover.jpg/.png/.webp/.bmp` 封面；
- 一首歌可挂多个音质；
- 曲库 JSON 导入、导出、合并；
- 当前曲库本地化导出为标准 ZIP；
- JS 曲库整理脚本；
- 播放、暂停、进度、音量；
- 播放队列、上一首 / 下一首、自动续播；
- 本地歌单保存、加载、追加、删除；
- 专辑墙与专辑详情；
- 专辑从曲库删除（只删曲库记录，不删磁盘文件）；
- MP3 ID3v2 / FLAC Vorbis Comment 基础 tag 读取；
- `.tly` 动态歌词滚动、翻译显示、逐字时间轴高亮；
- 扫描时识别并解密 `.ncm`，自动生成普通音频文件和内嵌封面后纳入曲库。

## 项目文档和脚本

- 曲库格式：[`docs/LIBRARY_SCHEMA.md`](docs/LIBRARY_SCHEMA.md)
- TLY 歌词格式：[`docs/TLY_FORMAT.md`](docs/TLY_FORMAT.md)
- JS 示例：[`scripts/normalize_album.js`](scripts/normalize_album.js)
- LRC 转 TLY：[`scripts/lrc_to_tly.js`](scripts/lrc_to_tly.js)
- 合并导入曲库：[`scripts/merge_import_library.js`](scripts/merge_import_library.js)
- TLY 示例：[`examples/demo.tly`](examples/demo.tly)

## 扫描时 LRC / NCM 行为

应用内置扫描器会：

- 优先使用同名 `.tly`；
- 如果没有 `.tly` 但有同名 `.lrc`，自动生成 `.tly`；
- 如果旁边存在同名 `.zh-CN.json` 翻译数组，也会写入 `tr=zh-CN` 行；
- 发现 `.ncm` 会调用内置转换器；
- 转换器会在 `.ncm` 同目录生成同名 `.mp3` / `.flac` / `.m4a` 等普通音频文件；
- 如果 `.ncm` 带内嵌封面，会生成同名 `.cover.jpg` / `.cover.png` 等封面 sidecar；
- 扫描器会继续把输出音频纳入曲库。

NCM 转换入口：

```text
src/NcmImportService.h
src/NcmImportService.cpp
```

核心函数：

```cpp
NcmImportResult NcmImportService::convertToOpenAudio(const QString &inputPath)
```

成功时返回 `Status::Converted`，并把 `outputAudioPath` 设为生成的普通音频文件路径。

## 曲库本地化 ZIP

顶部 **本地化ZIP** 会把当前曲库打包为标准 ZIP：

- `library.json`：包内相对路径版曲库索引；
- `music/...`：当前曲库引用到的音频、封面、歌词资源；
- ZIP 使用标准 store 条目，系统解压工具可直接读取；
- 当前实现不使用 ZIP64，超过 4GiB 或超过 65535 条目会返回错误。

相关入口：

```text
src/LibraryManager.h
src/LibraryManager.cpp
qml/Main.qml
```

核心函数：

```cpp
QString LibraryManager::exportLocalizedZip(const QString &fileUrl) const
```

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

## TLY 逐字高亮

行内可以使用绝对或相对时间标记：

```tly
[00:12.000]<00:12.000>逐<00:12.250>字<00:12.520>高亮
[00:16.000]<+00:00.000>相<+00:00.180>对<+00:00.360>时间
```

相对时间以当前歌词行开始时间为基准。

## 扩展指南

### 扩展音频扫描

音频扩展名集中在：

```text
src/LibraryManager.cpp
```

查找：

```cpp
kAudioExt
kEncryptedExt
```

新增普通音频格式时扩展 `kAudioExt`；新增加密格式时扩展 `kEncryptedExt` 并实现对应转换器。

### 扩展元数据读取

元数据读取入口：

```text
src/MetadataReader.h
src/MetadataReader.cpp
```

当前支持：

- MP3 ID3v2 基础文本帧；
- FLAC Vorbis Comment。

可以继续扩展：

- MP4/M4A atoms；
- OGG/Opus comments；
- APIC / PICTURE 内嵌封面读取；
- bitrate / sample rate。

### 扩展曲库整理脚本

脚本桥入口：

```text
src/ScriptBridge.h
src/ScriptBridge.cpp
scripts/
```

已有脚本可以作为模板：

```text
scripts/normalize_album.js
scripts/lrc_to_tly.js
scripts/merge_import_library.js
```

适合扩展：

- 批量清理标题；
- 合并专辑名；
- 标准化艺术家；
- 批量补 genre/year/track；
- 从外部数据源生成 `album.tnux.json`。

### 扩展 QML 界面

主界面：

```text
qml/Main.qml
```

常见扩展点：

- 顶部工具栏按钮；
- 曲库列表 delegate；
- 专辑墙 delegate；
- 队列和歌单面板；
- 右侧播放器和歌词面板。

后端功能一般通过 `Q_INVOKABLE` 暴露到 QML，例如：

```cpp
Q_INVOKABLE QString exportLocalizedZip(const QString &fileUrl) const;
```
