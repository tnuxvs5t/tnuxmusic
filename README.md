# tnuxmusic

一个基于 Qt 6 的桌面音乐平台 + 播放器。

## 技术栈

- Qt 6.11.1 / Qt Quick Controls 2：界面；
- Qt Multimedia：本地播放器；
- C++23：曲库、播放、歌词、脚本桥；
- QJSEngine：用 JavaScript 整理专辑和动态管理曲库；
- `.tly`：自定义动态歌词格式，支持时间轴、滚动、翻译和标签。

## 当前功能

- 扫描本地音乐文件夹；
- 自动识别常见音频格式：`mp3/flac/wav/m4a/aac/ogg/opus/...`；
- 自动关联同名 `.tly` 歌词；
- 自动关联同目录 `cover/folder/front/album/artwork` 封面；
- 一首歌可挂多个音质；
- 曲库 JSON 导入、导出、合并；
- JS 曲库整理脚本；
- 播放、暂停、进度、音量；
- `.tly` 动态歌词滚动和翻译显示。
- 播放队列、上一首 / 下一首、自动续播；
- 本地歌单保存、加载、追加、删除；
- 专辑墙与专辑详情；
- MP3 ID3v2 / FLAC Vorbis Comment 基础 tag 读取；
- `.tly` 逐字时间轴高亮。

## 构建

```bash
cmake --preset qt6-debug
cmake --build --preset qt6-debug
./build/qt6-debug/tnuxmusic
```

Qt 路径默认使用：

```text
/home/tnuzy/Qt/6.11.1/gcc_64
```

## 文档

- 曲库格式：[`docs/LIBRARY_SCHEMA.md`](docs/LIBRARY_SCHEMA.md)
- TLY 歌词格式：[`docs/TLY_FORMAT.md`](docs/TLY_FORMAT.md)
- JS 示例：[`scripts/normalize_album.js`](scripts/normalize_album.js)
- TLY 示例：[`examples/demo.tly`](examples/demo.tly)

## TLY 逐字高亮

行内可以使用绝对或相对时间标记：

```tly
[00:12.000]<00:12.000>逐<00:12.250>字<00:12.520>高亮
[00:16.000]<+00:00.000>相<+00:00.180>对<+00:00.360>时间
```

相对时间以当前歌词行开始时间为基准。
