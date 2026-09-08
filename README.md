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

### 2. 添加音乐与聆听

左侧 **添加音乐** 选择文件夹；支持普通音频、NCM 解密、封面和歌词。扫描、导入、合并与标签补全在后台进行，可继续使用播放器。点击状态栏的 **取消任务** 会放弃未提交的索引；已转换音频、歌词及已解包资源会保留。保存已经开始时需等待完成。关闭窗口可选择取消任务并退出，或等待不可取消的导出完成后退出。

**专辑** 页面支持名称、艺术家、年份和曲目数量排序，搜索同时匹配专辑和成员歌曲。**全部歌曲** 中双击或点播放按钮开始播放，点 **+** 加入队列，点 **⋯** 查看文件路径与选择音频版本。播放器支持顺序播放、列表循环、单曲循环和随机播放；单曲循环不影响手动切歌。随机模式每次随机选择不同队列位置，不保证完整一轮无重复。音量和播放模式会保存。

### 3. 整理专辑

- 专辑详情显示归组依据。按专辑艺术家（缺失时回退演唱者）、名称和年份归组，封面变化不会拆分专辑。
- **同目录合辑** 可归并没有专辑艺术家的同目录合辑；`CD1/CD2`、`Disc 1/Disc 2` 等明确的多碟目录使用共同的专辑目录。缺少年份时，只在同目录、同归组身份存在唯一已知年份时显示为同组，不改写原始标签；存在多个发行年份时保持分离。
- **整理 → 编辑** 可修改名称、专辑艺术家与年份；**合并到另一个专辑** 支持筛选名称、艺术家和年份，确认前显示目标与合并后数量。保留目标专辑信息和每首歌的演唱者。
- 勾选部分曲目后，可 **移入专辑** 或 **拆为新专辑**。新专辑有独立分组 ID，即使同名也不会自动混回去。
- 状态栏 **撤销** 或 `Ctrl+Z` 恢复最近一次索引修改，包括成功的扫描、导入、合并和整理；仅限本次运行内的一步。不会恢复此前被清除的队列，不删除转换或缓存文件。
- **更多操作 → 补全专辑标签** 读取音频中的专辑艺术家，只补缺失信息并跳过手动分组；没有标签的文件仍需手动整理。

### 4. 曲库与歌单

左侧 **更多操作** 提供导入并替换、导出 JSON、打包 ZIP、补全标签、脚本整理和清空曲库。左侧 **合并曲库** 保留现有曲目；导入替换会先确认。移除专辑和清空曲库只删除索引。

**播放队列** 右侧可保存、加载、追加和删除歌单。加载仅准备队列，点击开始播放后再播放。移除当前队列项不切断当前音频，下一首从原来的后继继续。歌单加载会跳过已不在曲库中的 ID，并报告数量。

同一个数据目录只允许一个应用实例。再次从系统应用启动会激活已有窗口，防止多个窗口覆盖索引或歌单。命令行合并也共用锁和事务：

```bash
node scripts/merge_import_library.js import.json /path/to/library.json
# 或
./build/qt6-release/tnuxmusic --merge-library import.json --library /path/to/library.json
```

命令行合并要求先关闭正在使用目标曲库的应用；脚本优先使用仓库 Release 构建，可通过 `TNUXMUSIC_BIN` 指定可执行文件。

快捷键：`Ctrl+F` 搜索、`Ctrl+O` 添加文件夹、空格播放/暂停（输入框和弹窗除外）、`Esc` 收起歌词或返回专辑列表，支持媒体播放/上一首/下一首按键。

## GitHub Release

项目会在推送与 CMake 版本匹配的 `vX.Y.Z` 标签后，完成 Linux 测试、两平台打包及打包程序验证，再自动发布 GitHub Release。手动分支运行默认只验证；详见 [发布流程](docs/RELEASING.md)。常见资产包括：

- `tnuxmusic-<tag>-linux-amd64.deb`
- `tnuxmusic-<tag>-windows-x64.zip`
- `tnuxmusic-<tag>-windows-x64-setup.exe`

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

**更多操作 → 打包音乐与曲库为 ZIP** 会把当前曲库打包为标准 ZIP：

- `library.json`：包内相对路径版曲库索引；
- `music/...`：当前曲库引用到的音频、封面、歌词资源；
- ZIP 使用标准 store 条目，系统解压工具可直接读取；
- 当前实现不使用 ZIP64，超过 4GiB 或超过 65535 条目会返回错误；
- 导入本地化 ZIP 时会解包到应用数据目录，并按解包目录解析 `library.json` 的相对路径，避免音乐和封面路径失效；合并 ZIP 时只解包当前合并实际需要的新资源，避免重复复制整包。

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
