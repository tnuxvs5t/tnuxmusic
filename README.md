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
