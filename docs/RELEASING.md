# Release workflow

Release 使用 Qt 6.8.3；本机开发默认 Qt 6.11.1。修改 JavaScript/QML 桥接或 Qt API 时，需要兼容 CI 版本，不能仅以开发机测试通过作为发布依据。

## 阶段

1. `prepare` 从 CMake 读取版本。标签必须与 `v<PROJECT_VERSION>` 完全一致；分支上的手动运行只允许验证。
2. Linux 构建全部目标，执行 QtTest 和实际进程 CLI 测试，保存 JUnit、测试日志和离屏截图。测试成功才打包 DEB，然后从 DEB 解出程序执行实际进程测试。
3. Windows 使用固定的 windows-2022/MSVC 环境，缓存 Qt 和 vcpkg 二进制依赖；编译应用，部署 Qt 和 OpenSSL DLL，清除构建工具 PATH 后验证版本与临时曲库合并，再生成 ZIP 和 Inno Setup EXE。Windows 尚未运行完整 QtTest。
4. `release` 只在两平台成功且本次要求发布时执行。只下载 `package-*`，明确校验三份安装包存在，生成 `SHA256SUMS.txt`，然后发布版本说明。诊断资料不混入 Release 下载。

## 发布

更新 CMake 项目版本与 `docs/RELEASE_NOTES.md`，本地构建测试后提交、推送分支和新的版本标签。标签 push 自动构建发布。

不要移动已推送的版本标签来修复构建。版本有源代码修复时递增补丁版本；仅网络/runner 临时故障时使用 GitHub 的 Re-run failed jobs。

## 手动验证与重试

```bash
gh workflow run release.yml --ref fix/audio-stability -f publish=false
# 仅当标签版本匹配且需要发布/重试时：
gh workflow run release.yml --ref v2.0.1 -f publish=true
```

手动分支验证也产出安装包，但不会创建 Release。失败时从 Actions 下载 `diagnostics-linux` 查看 JUnit、LastTest.log 和截图。Windows 配置日志在 `diagnostics-windows`，原生命令失败会立即终止对应步骤。

静态检查：`actionlint .github/workflows/release.yml`。发布版本校验入口：`scripts/release_version.py`；Windows 打包入口：`scripts/package_windows.ps1` 与 `assets/windows/installer.iss`。

## 2.0.1 本地发布验证

Qt 6.8.3 与 6.11.1 均通过 31 项 QtTest 和实际进程测试；使用 Qt 6.8.3 生成 DEB，从 DEB 解出后的运行验证通过。工作流通过 actionlint，版本校验覆盖分支验证、匹配标签发布、错误标签及分支发布拒绝。
