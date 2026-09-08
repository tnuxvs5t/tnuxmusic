import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1440; height: 900
    minimumWidth: 960; minimumHeight: 640
    visible: true
    title: "tnuxmusic"
    color: bg
    font.family: "Noto Sans CJK SC"
    font.pixelSize: 14
    Material.theme: Material.Dark
    Material.accent: accent
    Material.background: panel
    Material.foreground: ink

    readonly property color bg: "#101717"
    readonly property color panel: "#182222"
    readonly property color raised: "#233131"
    readonly property color line: "#304040"
    readonly property color accent: "#9bdfc5"
    readonly property color ink: "#edf4ef"
    readonly property color muted: "#a5b8b0"
    readonly property color faint: "#789188"
    property int currentTab: 1
    property string albumKey: ""
    property var albumInfo: ({})
    property var albumTracks: []
    property var playingTrack: ({})
    property string playingId: ""
    property bool showLyrics: false
    property string statusText: "准备好，听点音乐。"
    property int consecutiveQueueFailures: 0
    property string pendingImport: ""
    property var selectedTrackIds: []
    property bool closeAfterTask: false
    readonly property bool editingText: activeFocusItem !== null && activeFocusItem.cursorPosition !== undefined
    function toggleSelection(id) {
        var ids = selectedTrackIds.slice()
        var index = ids.indexOf(id)
        if (index < 0) ids.push(id); else ids.splice(index, 1)
        selectedTrackIds = ids
    }
    function prepareMerge(ids) {
        mergeAlbumDialog.sourceKey = albumKey
        mergeAlbumDialog.sourceTitle = albumInfo.album
        mergeAlbumDialog.selectedIds = ids.slice()
        mergeAlbumDialog.sourceCount = ids.length || albumInfo.trackCount
        mergeSearch.text = ""
        mergeTarget.model = albumModel.choices(albumKey, "")
        mergeTarget.currentIndex = mergeTarget.count > 0 ? 0 : -1
        mergeAlbumDialog.open()
    }
    function showTrack(entry) {
        trackInfoDialog.entry = libraryManager.track(entry.libraryRow)
        trackInfoDialog.open()
    }

    function fmt(ms) {
        var s = Math.max(0, Math.floor(ms / 1000))
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + s % 60
    }
    function say(message) { statusText = message }
    function run(kind, url) { libraryManager.startOperation(kind, url || "") }
    function refreshAlbum() {
        albumInfo = albumModel.info(albumKey)
        albumTracks = albumModel.tracksForKey(albumKey)
        if (!albumInfo.key) albumKey = ""
    }
    function openAlbum(key) {
        search.text = ""
        libraryManager.searchQuery = ""
        albumModel.searchQuery = ""
        currentTab = 1
        selectedTrackIds = []
        albumKey = key
        refreshAlbum()
    }
    function startRow(row, touchQueue) {
        if (row < 0) return false
        var track = libraryManager.track(row)
        if (!playerController.playFile(track.primaryPath || "")) {
            say(playerController.errorText)
            return false
        }
        playingId = track.id
        playingTrack = track
        if (touchQueue) {
            consecutiveQueueFailures = 0
            queueModel.playNowRow(row)
        }
        lyricModel.loadFromFile(track.lyrics || "")
        return true
    }
    function playAvailable(direction, reset, automatic) {
        if (reset) consecutiveQueueFailures = 0
        var attempts = queueModel.count
        while (attempts-- > 0) {
            var row = direction < 0 ? queueModel.previous() : queueModel.next(automatic || false)
            automatic = false
            if (row < 0) { playerController.stop(); say("队列已播放完毕"); return false }
            if (startRow(row, false)) return true
        }
        playerController.stop()
        say("播放队列中没有可用的音频文件")
        return false
    }
    function playQueue(index) {
        consecutiveQueueFailures = 0
        if (!startRow(queueModel.activate(index), false)) playAvailable(1, false)
    }
    function enqueueAlbum(play) {
        var rows = []
        for (var i = 0; i < albumTracks.length; ++i) rows.push(albumTracks[i].libraryRow)
        if (play) queueModel.clear()
        var count = queueModel.enqueueRows(rows)
        say("已加入队列 · " + count + " 首")
        if (play && count > 0) playQueue(0)
    }
    function togglePlayback() {
        if (playerController.source.length > 0) playerController.toggle()
        else if (queueModel.count > 0) playQueue(0)
    }
    function playlistNameOrCurrent() { return playlistName.text.trim() || playlistBox.currentText }

    onClosing: function(close) {
        if (libraryManager.busy) {
            close.accepted = false
            closeTaskDialog.open()
        }
    }
    Connections {
        target: libraryManager
        function onLastMessageChanged() { say(libraryManager.lastMessage) }
        function onOperationFinished(success, message) { if (closeAfterTask) root.close() }
        function onLibraryChanged() {
            var row = libraryManager.rowOfTrackId(playingId)
            if (row >= 0) playingTrack = libraryManager.track(row)
            else if (playingId.length > 0) {
                playerController.clearSource(); playingId = ""; playingTrack = ({})
                lyricModel.loadFromFile("")
            }
            selectedTrackIds = []
        }
    }
    Connections {
        target: albumModel
        function onAlbumsChanged() { if (albumKey) refreshAlbum() }
    }
    Connections {
        target: playerController
        function onFinished() { playAvailable(1, true, true) }
        function onFailed(message) {
            ++consecutiveQueueFailures
            if (queueModel.count > 0 && consecutiveQueueFailures < queueModel.count) playAvailable(1, false)
            else { playerController.stop(); say("播放失败：" + message) }
        }
    }
    Shortcut { sequence: "Ctrl+F"; onActivated: { if (currentTab === 2 || albumKey) { currentTab = 0; albumKey = "" } search.forceActiveFocus() } }
    Shortcut { sequence: "Space"; enabled: !editingText && !(root.Overlay.overlay && root.Overlay.overlay.visible); onActivated: togglePlayback() }
    Shortcut { sequence: "Escape"; enabled: !(root.Overlay.overlay && root.Overlay.overlay.visible); onActivated: { if (showLyrics) showLyrics = false; else if (albumKey) albumKey = ""; else search.clear() } }
    Shortcut { sequence: "Ctrl+Z"; enabled: !editingText && libraryManager.canUndoEdit && !libraryManager.busy; onActivated: say(libraryManager.undoLastEdit()) }
    Shortcut { sequence: "Ctrl+O"; enabled: !libraryManager.busy; onActivated: scanDialog.open() }
    Shortcut { sequence: "Media Play"; onActivated: togglePlayback() }
    Shortcut { sequence: "Media Next"; onActivated: playAvailable(1, true) }
    Shortcut { sequence: "Media Previous"; onActivated: playAvailable(-1, true) }

    FolderDialog { id: scanDialog; title: "选择音乐文件夹"; onAccepted: run("scan", selectedFolder) }
    FileDialog {
        id: importDialog; title: "导入曲库"; nameFilters: ["曲库 (*.json *.zip)"]
        onAccepted: { pendingImport = selectedFile; replaceDialog.open() }
    }
    FileDialog { id: mergeFileDialog; title: "合并另一份曲库"; nameFilters: ["曲库 (*.json *.zip)"]; onAccepted: run("merge", selectedFile) }
    FileDialog { id: exportDialog; title: "导出曲库索引"; fileMode: FileDialog.SaveFile; defaultSuffix: "json"; nameFilters: ["曲库索引 (*.json)"]; onAccepted: run("export", selectedFile) }
    FileDialog { id: zipDialog; title: "打包曲库与音乐"; fileMode: FileDialog.SaveFile; defaultSuffix: "zip"; nameFilters: ["音乐包 (*.zip)"]; onAccepted: run("zip", selectedFile) }
    FileDialog { id: scriptDialog; title: "运行曲库整理脚本"; nameFilters: ["JavaScript (*.js)"]; onAccepted: say(scriptBridge.runScript(selectedFile)) }

    component PromptDialog: Dialog {
        id: prompt
        Material.elevation: 0
        background: Rectangle { color: root.panel; radius: 14; border.color: root.line }
        header: Label { text: prompt.title; color: root.ink; font.pixelSize: 22; padding: 24; bottomPadding: 8; elide: Text.ElideRight }
        Overlay.modal: Rectangle { color: "#aa000000" }
    }
    component Action: Button {
        id: button
        property bool primary: false
        property bool selected: false
        property string glyph: ""
        implicitHeight: 40
        implicitWidth: Math.max(40, contentItem.implicitWidth + 28)
        leftPadding: 14; rightPadding: 14
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: text || ToolTip.text
        contentItem: Row {
            spacing: 8
            Text { text: button.glyph; visible: text.length > 0; color: button.primary ? "#12372c" : button.selected ? root.accent : root.muted; font.pixelSize: 18; anchors.verticalCenter: parent.verticalCenter }
            Text { text: button.text; color: button.primary ? "#12372c" : button.selected ? root.accent : root.ink; font.pixelSize: 13; font.weight: button.primary ? Font.DemiBold : Font.Normal; anchors.verticalCenter: parent.verticalCenter }
        }
        background: Rectangle {
            radius: 9
            color: button.primary ? (button.down ? "#76bea3" : button.hovered ? "#b5efd9" : root.accent)
                : button.selected ? "#29483d" : button.hovered ? root.raised : "transparent"
            border.width: button.activeFocus ? 2 : 0
            border.color: root.accent
        }
        opacity: enabled ? 1 : 0.38
        ToolTip.visible: hovered && ToolTip.text.length > 0
        ToolTip.delay: 600
    }
    component Cover: Rectangle {
        id: cover
        property string url: ""
        property int resolution: 320
        radius: 10; color: root.raised
        clip: true
        Text { anchors.centerIn: parent; text: "♪"; font.pixelSize: Math.min(parent.width * 0.36, 64); color: "#527367" }
        Image {
            anchors.fill: parent
            source: cover.url
            sourceSize: Qt.size(cover.resolution, cover.resolution)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
    }
    component Empty: Column {
        property string heading: "这里还很安静"
        property string detail: "添加你的音乐，开始聆听。"
        spacing: 12
        Text { text: "♫"; color: root.accent; font.pixelSize: 46; anchors.horizontalCenter: parent.horizontalCenter }
        Text { text: parent.heading; color: root.ink; font.pixelSize: 22; anchors.horizontalCenter: parent.horizontalCenter }
        Text { text: parent.detail; color: root.muted; font.pixelSize: 13; anchors.horizontalCenter: parent.horizontalCenter }
    }
    component TrackRow: Rectangle {
        id: trackRow
        required property var entry
        property int number: 0
        property bool queueEntry: false
        property int queueIndex: -1
        property bool selectable: false
        readonly property bool highlighted: playingId.length > 0 && playingId === entry.id && (!queueEntry || queueIndex === queueModel.currentIndex)
        signal playRequested()
        height: 66
        radius: 8
        color: highlighted ? "#243d34" : rowHover.hovered ? root.panel : "transparent"
        HoverHandler { id: rowHover }
        TapHandler { onDoubleTapped: trackRow.playRequested() }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 8; spacing: 14
            CheckBox { visible: trackRow.selectable; checked: selectedTrackIds.indexOf(trackRow.entry.id) >= 0; onToggled: toggleSelection(trackRow.entry.id); Accessible.name: "选择 " + (trackRow.entry.title || "曲目") }
            Text { Layout.preferredWidth: 26; text: trackRow.highlighted ? "♫" : String(trackRow.number + 1).padStart(2, "0"); color: trackRow.highlighted ? root.accent : root.faint; font.pixelSize: 12 }
            Cover { Layout.preferredWidth: 44; Layout.preferredHeight: 44; radius: 5; resolution: 88; url: trackRow.entry.coverUrl || "" }
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 130; spacing: 3
                Text { Layout.fillWidth: true; text: trackRow.entry.title || "未命名曲目"; color: trackRow.highlighted ? root.accent : root.ink; elide: Text.ElideRight; font.pixelSize: 14 }
                Text { Layout.fillWidth: true; text: trackRow.entry.artist || "未知艺术家"; color: root.muted; elide: Text.ElideRight; font.pixelSize: 12 }
            }
            Text { Layout.preferredWidth: Math.max(100, trackRow.width * 0.22); visible: trackRow.width > 640; text: trackRow.entry.album || "未命名专辑"; color: root.muted; elide: Text.ElideRight; font.pixelSize: 12 }
            Text { Layout.preferredWidth: 88; visible: trackRow.width > 780; text: trackRow.entry.qualitiesText || ""; color: root.faint; elide: Text.ElideRight; font.pixelSize: 11 }
            Action { text: ""; glyph: "▷"; ToolTip.text: "播放 " + (trackRow.entry.title || "曲目"); onClicked: trackRow.playRequested() }
            Action { text: "⋯"; ToolTip.text: "文件与音质详情"; visible: !trackRow.queueEntry; onClicked: showTrack(trackRow.entry) }
            Action {
                text: trackRow.queueEntry ? "移除" : "+"
                ToolTip.text: trackRow.queueEntry ? "从队列移除" : "加入播放队列"
                onClicked: {
                    if (trackRow.queueEntry) queueModel.removeAt(trackRow.queueIndex)
                    else { var result = queueModel.enqueueRow(trackRow.entry.libraryRow); say(result >= 0 ? "已加入播放队列" : "曲目已变化，请刷新后重试") }
                }
            }
        }
    }

    PromptDialog {
        id: closeTaskDialog; title: "退出前完成曲库任务"; modal: true; anchors.centerIn: parent; width: 450
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { width: parent.width; wrapMode: Text.WordWrap; text: libraryManager.canCancel ? "确定后取消任务并退出。当前文件处理结束后关闭，已生成的文件会保留。" : "确定后等待当前保存或导出完成，再自动退出。" }
        onAccepted: { closeAfterTask = true; libraryManager.cancelOperation(); if (!libraryManager.busy) root.close() }
    }
    PromptDialog {
        id: trackInfoDialog; objectName: "trackInfoDialog"; title: "文件与音质"; modal: true; anchors.centerIn: parent; width: Math.min(620, root.width - 80)
        property var entry: ({})
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            spacing: 12
            Label { Layout.fillWidth: true; text: trackInfoDialog.entry.title || ""; font.pixelSize: 20; wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; text: (trackInfoDialog.entry.artist || "未知艺术家") + " · " + (trackInfoDialog.entry.album || "未命名专辑"); color: root.muted; wrapMode: Text.Wrap }
            Label { text: "音频版本 · 选择后从头播放"; color: root.accent }
            ListView {
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(260, contentHeight); clip: true
                model: trackInfoDialog.entry.qualities || []
                ScrollBar.vertical: ScrollBar {}
                delegate: ColumnLayout {
                    required property var modelData
                    width: ListView.view.width - 12; spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: (modelData.label || modelData.codec || "音频") + (modelData.bitrate ? " · " + modelData.bitrate + " kbps" : ""); elide: Text.ElideRight }
                        Action { text: "播放此版本"; onClicked: {
                            if (playerController.playFile(modelData.path)) {
                                playingTrack = trackInfoDialog.entry; playingId = playingTrack.id
                                consecutiveQueueFailures = 0; queueModel.playNowRow(libraryManager.rowOfTrackId(playingId))
                                lyricModel.loadFromFile(playingTrack.lyrics || ""); trackInfoDialog.close()
                            } else say(playerController.errorText)
                        } }
                    }
                    TextArea { Layout.fillWidth: true; text: modelData.path; readOnly: true; selectByMouse: true; wrapMode: Text.WrapAnywhere; color: root.muted; font.pixelSize: 11; background: Rectangle { color: root.raised; radius: 6 } }
                }
            }
        }
    }
    PromptDialog {
        id: splitAlbumDialog; objectName: "splitAlbumDialog"; title: "拆分为独立专辑"; modal: true; anchors.centerIn: parent; width: 470
        property string sourceKey: ""
        property var selectedIds: []
        standardButtons: Dialog.Save | Dialog.Cancel
        onOpened: standardButton(Dialog.Save).enabled = Qt.binding(function() { return splitTitle.text.trim().length > 0 && !libraryManager.busy })
        ColumnLayout {
            anchors.fill: parent; spacing: 10
            Label { text: "将选中的 " + splitAlbumDialog.selectedIds.length + " 首移入新的独立专辑"; color: root.accent }
            TextField { id: splitTitle; Layout.fillWidth: true; placeholderText: "新专辑名称"; selectByMouse: true }
            TextField { id: splitArtist; Layout.fillWidth: true; placeholderText: "专辑艺术家"; selectByMouse: true }
            Label { text: "发行年份（0 表示未知）"; color: root.muted }
            SpinBox { id: splitYear; from: 0; to: 9999; editable: true }
            Label { Layout.fillWidth: true; text: "即使名称相同，也会建立独立分组。可用“撤销”恢复。"; wrapMode: Text.Wrap; color: root.muted }
        }
        onAccepted: say(albumModel.splitTracks(sourceKey, selectedIds, splitTitle.text, splitArtist.text, splitYear.value))
    }
    PromptDialog {
        id: replaceDialog; title: "替换当前曲库？"; modal: true; anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { width: 390; wrapMode: Text.WordWrap; text: "将用导入文件替换当前 " + libraryManager.count + " 首曲目。想保留当前曲库，请取消并使用“合并曲库”。磁盘音乐文件会保留。" }
        onAccepted: run("import", pendingImport)
    }
    PromptDialog {
        id: clearDialog; title: "清空曲库记录？"; modal: true; anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { width: 350; wrapMode: Text.WordWrap; text: "将移除全部曲库记录，磁盘音乐文件会保留。建议先导出曲库索引备份。" }
        onAccepted: say(libraryManager.clearLibrary())
    }
    PromptDialog {
        id: removeAlbumDialog; title: "从曲库移除专辑？"; modal: true; anchors.centerIn: parent
        property string targetKey: ""
        property string albumTitle: ""
        property int trackCount: 0
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { width: 350; wrapMode: Text.WordWrap; text: "移除《" + removeAlbumDialog.albumTitle + "》的 " + removeAlbumDialog.trackCount + " 首曲目。磁盘音乐文件会保留。" }
        onAccepted: say(albumModel.removeAlbum(targetKey))
    }
    PromptDialog {
        id: editAlbumDialog; objectName: "editAlbumDialog"; title: "编辑专辑"; modal: true; anchors.centerIn: parent; width: 440
        property string targetKey: ""
        standardButtons: Dialog.Save | Dialog.Cancel
        onOpened: standardButton(Dialog.Save).enabled = Qt.binding(function() { return albumTitle.text.trim().length > 0 && !libraryManager.busy })
        ColumnLayout {
            anchors.fill: parent; spacing: 8
            Label { text: "专辑名称"; color: root.muted }
            TextField { id: albumTitle; Layout.fillWidth: true; selectByMouse: true }
            Label { text: "专辑艺术家"; color: root.muted }
            TextField { id: albumArtist; Layout.fillWidth: true; selectByMouse: true; placeholderText: "合辑可填写 Various Artists" }
            Label { text: "发行年份（0 表示未知）"; color: root.muted }
            SpinBox { id: albumYear; from: 0; to: 9999; editable: true }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "演唱者与音乐文件保持原样。修改会保存到曲库索引。"; color: root.muted; font.pixelSize: 12 }
        }
        onAccepted: say(albumModel.editAlbum(targetKey, albumTitle.text, albumArtist.text, albumYear.value))
    }
    PromptDialog {
        id: mergeAlbumDialog; objectName: "mergeAlbumDialog"; title: "合并到另一个专辑"; modal: true; anchors.centerIn: parent; width: 530
        property string sourceKey: ""
        property string sourceTitle: ""
        property int sourceCount: 0
        property var selectedIds: []
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: standardButton(Dialog.Ok).enabled = Qt.binding(function() { return mergeTarget.currentIndex >= 0 && !libraryManager.busy })
        ColumnLayout {
            anchors.fill: parent; spacing: 14
            Label { Layout.fillWidth: true; text: "来源：" + mergeAlbumDialog.sourceTitle + " · " + mergeAlbumDialog.sourceCount + " 首"; elide: Text.ElideRight }
            Label { text: "目标专辑 · 同名优先"; color: root.muted }
            TextField { id: mergeSearch; objectName: "mergeSearch"; Layout.fillWidth: true; placeholderText: "筛选名称、艺术家或年份"; selectByMouse: true
                onTextChanged: { mergeTarget.model = albumModel.choices(mergeAlbumDialog.sourceKey, text); mergeTarget.currentIndex = mergeTarget.count > 0 ? 0 : -1 }
            }
            ComboBox { id: mergeTarget; objectName: "mergeTarget"; Layout.fillWidth: true; textRole: "label"; valueRole: "key"; displayText: currentIndex >= 0 ? currentText : "没有匹配的专辑" }
            Label {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: root.accent
                text: mergeTarget.currentIndex >= 0 ? "合并后共 " + (mergeAlbumDialog.sourceCount + mergeTarget.model[mergeTarget.currentIndex].count) + " 首，使用目标专辑名称、专辑艺术家和年份。" : "请选择目标专辑"
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "保留每首歌的演唱者、音质和歌单引用；不复制音乐文件。此分组会保存，重新启动后仍然有效。"; color: root.muted; font.pixelSize: 12 }
        }
        onAccepted: {
            if (mergeTarget.currentIndex >= 0) say(selectedIds.length > 0 ? albumModel.moveTracks(sourceKey, selectedIds, mergeTarget.currentValue) : albumModel.mergeAlbums(sourceKey, mergeTarget.currentValue))
        }
    }
    Menu {
        id: libraryMenu
        MenuItem { text: "合并曲库…"; enabled: !libraryManager.busy; onTriggered: mergeFileDialog.open() }
        MenuItem { text: "导入并替换曲库…"; enabled: !libraryManager.busy; onTriggered: importDialog.open() }
        MenuSeparator {}
        MenuItem { text: "导出曲库索引…"; enabled: !libraryManager.busy; onTriggered: exportDialog.open() }
        MenuItem { text: "打包音乐与曲库为 ZIP…"; enabled: !libraryManager.busy; onTriggered: zipDialog.open() }
        MenuItem { text: "补全专辑标签"; enabled: !libraryManager.busy; onTriggered: run("refresh") }
        MenuItem { text: "运行整理脚本…"; enabled: !libraryManager.busy; onTriggered: scriptDialog.open() }
        MenuSeparator {}
        MenuItem { text: "撤销上次整理"; enabled: libraryManager.canUndoEdit && !libraryManager.busy; onTriggered: say(libraryManager.undoLastEdit()) }
        MenuItem { text: "清空曲库…"; enabled: !libraryManager.busy; onTriggered: clearDialog.open() }
    }

    Rectangle {
        id: sidebar; width: root.width < 1120 ? 168 : 204; color: "#131d1b"
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: playerBar.top
        Rectangle { width: 1; color: root.line; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: root.height < 760 ? 12 : 18; spacing: root.height < 760 ? 2 : 6
            RowLayout {
                Layout.topMargin: 14; Layout.bottomMargin: root.height < 760 ? 12 : 38; spacing: 9
                Rectangle { width: 30; height: 30; radius: 9; color: root.accent; Text { anchors.centerIn: parent; text: "♪"; color: "#12372c"; font.pixelSize: 23 } }
                Text { text: "tnuxmusic"; color: root.ink; font.pixelSize: root.width < 1120 ? 16 : 19; font.weight: Font.DemiBold }
            }
            Text { text: "你的音乐"; color: root.faint; font.pixelSize: 11; Layout.leftMargin: 12; Layout.bottomMargin: 12 }
            Action { Layout.fillWidth: true; text: "专辑"; glyph: "▦"; selected: currentTab === 1; onClicked: { currentTab = 1; albumKey = ""; search.text = "" } }
            Action { Layout.fillWidth: true; text: "全部歌曲"; glyph: "♫"; selected: currentTab === 0; onClicked: { currentTab = 0; search.text = "" } }
            Action { Layout.fillWidth: true; text: "播放队列"; glyph: "≡"; selected: currentTab === 2; onClicked: { currentTab = 2; search.text = "" } }
            Rectangle { Layout.fillWidth: true; Layout.topMargin: root.height < 760 ? 8 : 22; Layout.bottomMargin: root.height < 760 ? 6 : 16; height: 1; color: root.line }
            Text { text: "曲库管理"; color: root.faint; font.pixelSize: 11; Layout.leftMargin: 12; Layout.bottomMargin: 12 }
            Action { Layout.fillWidth: true; text: "添加音乐"; glyph: "+"; enabled: !libraryManager.busy; onClicked: scanDialog.open() }
            Action { Layout.fillWidth: true; text: "合并曲库"; enabled: !libraryManager.busy; onClicked: mergeFileDialog.open() }
            Action { Layout.fillWidth: true; text: "更多操作"; glyph: "⋯"; onClicked: libraryMenu.popup() }
            Item { Layout.fillHeight: true }
            Rectangle {
                Layout.fillWidth: true; height: root.height < 760 ? 62 : 82; radius: 10; color: root.panel
                Column { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; spacing: 6
                    Text { text: libraryManager.count + " 首音乐"; color: root.ink; font.pixelSize: 14 }
                    Text { text: albumModel.count + " 张专辑 · 本地曲库"; color: root.muted; font.pixelSize: 11 }
                }
            }
            Text { visible: root.height >= 760; text: "让音乐回到音乐。"; color: root.faint; font.pixelSize: 11; Layout.leftMargin: 12; Layout.topMargin: 12; Layout.bottomMargin: 4 }
        }
    }

    ColumnLayout {
        id: workspace
        anchors.left: sidebar.right; anchors.right: lyricsPanel.visible && root.width >= 1280 ? lyricsPanel.left : parent.right
        anchors.top: parent.top; anchors.bottom: playerBar.top
        anchors.margins: root.width < 1120 ? 20 : 32; spacing: 18
        RowLayout {
            Layout.fillWidth: true; spacing: 18
            ColumnLayout {
                spacing: 5; Layout.fillWidth: true; Layout.minimumWidth: 180
                Layout.preferredWidth: Math.max(180, workspace.width - 430)
                Text { text: currentTab === 0 ? "全部歌曲" : currentTab === 2 ? "播放队列" : albumKey ? "专辑详情" : "专辑收藏"; color: root.ink; font.pixelSize: 30; font.weight: Font.DemiBold }
                Text { text: currentTab === 0 ? libraryManager.count + " 首本地音乐 · 双击播放，+ 加入队列" : currentTab === 2 ? queueModel.count + " 首待播 · 保存为歌单，随时接着听" : albumKey ? "聆听、选曲与整理专辑归属" : "你的唱片架 · " + albumModel.count + " 张专辑 / " + libraryManager.count + " 首音乐"; color: root.muted; font.pixelSize: 12 }
            }
            TextField {
                id: search; objectName: "librarySearch"; visible: currentTab !== 2 && (currentTab !== 1 || !albumKey)
                Layout.preferredWidth: Math.min(270, workspace.width * 0.36)
                placeholderText: currentTab === 1 ? "搜索专辑或艺术家  /  Ctrl F" : "搜索歌曲、艺术家、专辑"
                selectByMouse: true; color: root.ink; rightPadding: 34
                Action { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; implicitWidth: 30; width: 30; leftPadding: 6; rightPadding: 6; text: "×"; visible: search.text.length > 0; ToolTip.text: "清除搜索"; onClicked: search.clear() }
                background: Rectangle { radius: 9; color: root.panel; border.color: search.activeFocus ? root.accent : root.line }
                onTextChanged: searchDelay.restart()
            }
            Action { text: "添加音乐"; primary: true; visible: workspace.width > 680; enabled: !libraryManager.busy; onClicked: scanDialog.open() }
        }
        Timer { id: searchDelay; interval: 180; onTriggered: { libraryManager.searchQuery = search.text; albumModel.searchQuery = search.text } }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Rectangle { width: 5; height: 5; radius: 3; color: libraryManager.busy ? root.accent : "#8bc9ac" }
            Text { Layout.fillWidth: true; text: statusText; color: libraryManager.busy ? root.accent : root.faint; elide: Text.ElideRight; font.pixelSize: 11
                HoverHandler { id: statusHover }
                ToolTip.visible: statusHover.hovered; ToolTip.text: statusText
            }
            Action { text: "撤销"; visible: libraryManager.canUndoEdit && !libraryManager.busy; implicitHeight: 26; ToolTip.text: "撤销最近一次曲库修改（本次启动内）"; onClicked: say(libraryManager.undoLastEdit()) }
            Action { text: "取消任务"; visible: libraryManager.canCancel; implicitHeight: 26; onClicked: libraryManager.cancelOperation() }
            BusyIndicator { implicitWidth: 20; implicitHeight: 20; visible: libraryManager.busy; running: visible }
        }
        ProgressBar { Layout.fillWidth: true; Layout.preferredHeight: 2; visible: libraryManager.busy; indeterminate: true }
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; currentIndex: currentTab
            Item {
                ListView {
                    id: songList; objectName: "songList"; anchors.fill: parent; clip: true; model: libraryManager; spacing: 2; reuseItems: true
                    ScrollBar.vertical: ScrollBar {}
                    delegate: TrackRow {
                        required property int index
                        required property var model
                        width: songList.width - 12; number: index
                        entry: ({id: model.trackId, title: model.title, artist: model.artist, album: model.album, coverUrl: model.coverUrl, qualitiesText: model.qualitiesText, libraryRow: model.sourceRow})
                        onPlayRequested: startRow(model.sourceRow, true)
                    }
                }
                Empty { anchors.centerIn: parent; visible: songList.count === 0; heading: search.text ? "没有找到歌曲" : "收藏从第一首歌开始"; detail: search.text ? "试试其他标题、专辑或艺术家。" : "点击“添加音乐”，选择你的音乐文件夹。" }
            }
            Item {
                ColumnLayout {
                    anchors.fill: parent; visible: !albumKey; spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: albumGrid.count + " 张专辑"; color: root.muted; font.pixelSize: 12 }
                        Item { Layout.fillWidth: true }
                        ComboBox { implicitWidth: 136; model: ["专辑名称", "专辑艺术家", "发行年份 ↓", "曲目数量 ↓"]; currentIndex: albumModel.sortMode; onActivated: albumModel.sortMode = currentIndex; Accessible.name: "专辑排序" }
                        CheckBox {
                            text: "同目录合辑"; checked: albumModel.autoMergeAlbums
                            onToggled: albumModel.autoMergeAlbums = checked
                            ToolTip.visible: hovered; ToolTip.text: "按专辑目录归并无专辑艺术家的合辑，支持 CD1/CD2 多碟目录。缺年份仅在同目录有唯一已知年份时归组；不会改写标签。"
                        }
                    }
                    GridView {
                        id: albumGrid; objectName: "albumGrid"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        cellWidth: width / Math.max(2, Math.floor(width / 195))
                        cellHeight: cellWidth + 76
                        model: albumModel
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Item {
                            id: albumTile
                            required property string albumKey
                            required property string album
                            required property string artist
                            required property string coverUrl
                            required property int trackCount
                            required property int year
                            width: albumGrid.cellWidth; height: albumGrid.cellHeight
                            Rectangle { anchors.fill: parent; anchors.rightMargin: 12; anchors.bottomMargin: 8; radius: 12; color: tileHover.hovered ? root.panel : "transparent" }
                            HoverHandler { id: tileHover }
                            ToolTip.visible: tileHover.hovered; ToolTip.delay: 650
                            ToolTip.text: albumTile.album + "\n" + albumTile.artist + " · " + (albumTile.year || "年份未知") + " · " + albumTile.trackCount + " 首"
                            Accessible.role: Accessible.Button; Accessible.name: album + "，" + artist
                            activeFocusOnTab: true
                            Keys.onReturnPressed: openAlbum(albumTile.albumKey)
                            Keys.onSpacePressed: openAlbum(albumTile.albumKey)
                            Column {
                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 7; anchors.rightMargin: 19; spacing: 8
                                Cover {
                                    width: parent.width; height: width; url: albumTile.coverUrl; resolution: 400
                                    Action { anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 10; text: "打开"; primary: true; visible: tileHover.hovered; onClicked: openAlbum(albumTile.albumKey) }
                                }
                                Text { width: parent.width; text: albumTile.album; color: root.ink; font.pixelSize: 14; font.weight: Font.Medium; elide: Text.ElideRight }
                                Text { width: parent.width; text: albumTile.artist + " · " + (albumTile.year ? albumTile.year + " · " : "") + albumTile.trackCount + " 首"; color: root.muted; font.pixelSize: 11; elide: Text.ElideRight }
                            }
                            TapHandler { onTapped: openAlbum(albumTile.albumKey) }
                        }
                        Empty { anchors.centerIn: parent; visible: albumGrid.count === 0; heading: search.text ? "没有找到专辑" : "你的专辑，值得被收藏"; detail: search.text ? "试试其他专辑名或艺术家。" : "添加音乐后，专辑会在这里自动归档。" }
                    }
                }
                ColumnLayout {
                    anchors.fill: parent; visible: albumKey.length > 0; spacing: 18
                    Action { text: "所有专辑"; glyph: "‹"; onClicked: { albumKey = ""; selectedTrackIds = [] } }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 24
                        Cover { Layout.preferredWidth: root.height < 760 ? 104 : 156; Layout.preferredHeight: Layout.preferredWidth; url: albumInfo.coverUrl || "" }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 10
                            Text { Layout.fillWidth: true; text: albumInfo.grouping || "专辑"; color: root.accent; font.pixelSize: 11; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: albumInfo.album || ""; color: root.ink; font.pixelSize: 26; font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: (albumInfo.artist || "") + " · " + (albumInfo.year || "年份未知") + " · " + (albumInfo.trackCount || 0) + " 首"; color: root.muted; font.pixelSize: 12; elide: Text.ElideRight }
                            RowLayout {
                                Action { text: "播放专辑"; glyph: "▷"; primary: true; onClicked: enqueueAlbum(true) }
                                Action { text: "加入队列"; onClicked: enqueueAlbum(false) }
                                Action { text: "整理"; enabled: !libraryManager.busy; onClicked: albumMenu.popup() }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        CheckBox { text: selectedTrackIds.length ? "已选 " + selectedTrackIds.length + " 首" : "选择曲目"; checked: albumTracks.length > 0 && selectedTrackIds.length === albumTracks.length
                            onToggled: { var ids = []; if (checked) for (var i = 0; i < albumTracks.length; ++i) ids.push(albumTracks[i].id); selectedTrackIds = ids }
                        }
                        Item { Layout.fillWidth: true }
                        Action { text: "移入专辑"; visible: selectedTrackIds.length > 0; enabled: !libraryManager.busy && albumModel.count > 1; onClicked: prepareMerge(selectedTrackIds) }
                        Action { text: "拆为新专辑"; visible: selectedTrackIds.length > 0; enabled: !libraryManager.busy && selectedTrackIds.length < albumTracks.length; onClicked: {
                            splitAlbumDialog.sourceKey = albumKey; splitAlbumDialog.selectedIds = selectedTrackIds.slice()
                            splitTitle.text = albumInfo.album; splitArtist.text = albumInfo.artist; splitYear.value = albumInfo.year; splitAlbumDialog.open()
                        } }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.line }
                    ListView {
                        id: detailList; objectName: "albumTrackList"; Layout.fillWidth: true; Layout.fillHeight: true; model: albumTracks; clip: true; reuseItems: true
                        ScrollBar.vertical: ScrollBar {}
                        delegate: TrackRow { required property var modelData; required property int index; width: detailList.width - 12; entry: modelData; number: index; selectable: true; onPlayRequested: startRow(modelData.libraryRow, true) }
                    }
                }
                Menu {
                    id: albumMenu
                    MenuItem { text: "编辑名称、艺术家与年份…"; onTriggered: {
                        editAlbumDialog.targetKey = albumKey; albumTitle.text = albumInfo.album; albumArtist.text = albumInfo.artist; albumYear.value = albumInfo.year; editAlbumDialog.open()
                    } }
                    MenuItem { text: "合并到另一个专辑…"; enabled: albumModel.count > 1; onTriggered: {
                        prepareMerge([])
                    } }
                    MenuSeparator {}
                    MenuItem { text: "从曲库移除…"; onTriggered: {
                        removeAlbumDialog.targetKey = albumKey; removeAlbumDialog.albumTitle = albumInfo.album; removeAlbumDialog.trackCount = albumInfo.trackCount; removeAlbumDialog.open()
                    } }
                }
            }
            RowLayout {
                spacing: 22
                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    RowLayout { Layout.fillWidth: true
                        Action { text: "开始播放"; primary: true; enabled: queueModel.count > 0; onClicked: playQueue(0) }
                        Item { Layout.fillWidth: true }
                        Action { text: "清空队列"; enabled: queueModel.count > 0; onClicked: queueModel.clear() }
                    }
                    ListView {
                        id: queueList; objectName: "queueList"; Layout.fillWidth: true; Layout.fillHeight: true; model: queueModel; clip: true; reuseItems: true
                        ScrollBar.vertical: ScrollBar {}
                        delegate: TrackRow {
                            required property var model
                            required property int index
                            width: queueList.width - 12; number: index; queueEntry: true; queueIndex: index
                            entry: ({id: model.trackId, title: model.title, artist: model.artist, album: model.album, coverUrl: model.coverUrl, libraryRow: model.libraryRow})
                            onPlayRequested: playQueue(index)
                        }
                        Empty { anchors.centerIn: parent; visible: queueList.count === 0; heading: "接下来，听什么？"; detail: "在歌曲旁点 +，或将整张专辑加入队列。" }
                    }
                }
                Rectangle {
                    Layout.preferredWidth: root.width < 1120 ? 206 : 246; Layout.fillHeight: true; radius: 12; color: root.panel
                    Flickable {
                        anchors.fill: parent; anchors.margins: 18; clip: true
                        contentHeight: playlistControls.implicitHeight
                        flickableDirection: Flickable.VerticalFlick
                        ScrollBar.vertical: ScrollBar {}
                        ColumnLayout {
                        id: playlistControls; width: parent.width; spacing: 12
                        Text { text: "我的歌单"; color: root.ink; font.pixelSize: 19; Layout.bottomMargin: 8 }
                        ComboBox { id: playlistBox; Layout.fillWidth: true; model: queueModel.playlistNames }
                        TextField { id: playlistName; Layout.fillWidth: true; placeholderText: "输入新歌单名称"; selectByMouse: true }
                        Action { Layout.fillWidth: true; text: "保存当前队列"; primary: true; onClicked: say(queueModel.saveQueueAsPlaylist(playlistNameOrCurrent())) }
                        Action { Layout.fillWidth: true; text: "加载歌单"; enabled: playlistBox.count > 0; onClicked: say(queueModel.loadPlaylist(playlistBox.currentText, true)) }
                        Action { Layout.fillWidth: true; text: "追加到队列"; enabled: playlistBox.count > 0; onClicked: say(queueModel.loadPlaylist(playlistBox.currentText, false)) }
                        Action { Layout.fillWidth: true; text: "删除选中歌单"; enabled: playlistBox.count > 0; onClicked: playlistDeleteDialog.open() }
                        }
                    }
                }
            }
        }
    }
    PromptDialog {
        id: playlistDeleteDialog; title: "删除歌单？"; modal: true; anchors.centerIn: parent; standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: "删除歌单「" + playlistBox.currentText + "」。曲库中的歌曲会保留。" }
        onAccepted: say(queueModel.deletePlaylist(playlistBox.currentText))
    }
    Rectangle {
        anchors.left: sidebar.right; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: playerBar.top
        visible: showLyrics && root.width < 1280; color: "#99080e0b"; z: 19
        MouseArea { anchors.fill: parent; onClicked: showLyrics = false; onWheel: function(wheel) { wheel.accepted = true } }
    }
    Rectangle {
        id: lyricsPanel; z: 20; visible: showLyrics; width: root.width < 1280 ? 360 : 320; color: root.panel
        anchors.top: parent.top; anchors.right: parent.right; anchors.bottom: playerBar.top
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 20; spacing: 18
            RowLayout { Layout.fillWidth: true
                Text { text: "正在聆听"; color: root.muted; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                Action { text: "×"; ToolTip.text: "收起歌词"; onClicked: showLyrics = false }
            }
            Cover { Layout.fillWidth: true; Layout.preferredHeight: root.height < 760 ? 100 : width; visible: root.height >= 700; url: playingTrack.coverUrl || ""; resolution: 600 }
            Text { Layout.fillWidth: true; text: playingTrack.title || "等待播放"; color: root.ink; font.pixelSize: 20; elide: Text.ElideRight }
            ListView {
                id: lyricList; Layout.fillWidth: true; Layout.fillHeight: true; model: lyricModel; clip: true; spacing: 22
                ScrollBar.vertical: ScrollBar {}
                delegate: Column {
                    required property string richText
                    required property string translation
                    required property bool active
                    width: lyricList.width - 12; spacing: 8
                    Text { width: parent.width; text: parent.richText; textFormat: Text.RichText; color: parent.active ? root.ink : root.muted; font.pixelSize: parent.active ? 21 : 16; wrapMode: Text.Wrap }
                    Text { width: parent.width; text: parent.translation; visible: text.length > 0; color: root.muted; font.pixelSize: 12; wrapMode: Text.Wrap }
                }
                Text { anchors.centerIn: parent; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: "此刻，让旋律说话。\n暂无歌词"; color: root.faint; lineHeight: 1.8; visible: lyricList.count === 0; font.pixelSize: 13 }
                Connections { target: lyricModel; function onActiveIndexChanged() { if (lyricModel.activeIndex >= 0) lyricList.positionViewAtIndex(lyricModel.activeIndex, ListView.Center) } }
            }
        }
    }
    Rectangle {
        id: playerBar; z: 30; height: 112; color: "#182522"; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        Rectangle { height: 1; color: root.line; width: parent.width }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 26; anchors.rightMargin: 26; spacing: 24
            Cover { Layout.preferredWidth: 62; Layout.preferredHeight: 62; radius: 7; resolution: 124; url: playingTrack.coverUrl || "" }
            ColumnLayout {
                Layout.preferredWidth: Math.min(230, root.width * 0.18); spacing: 5
                Text { Layout.fillWidth: true; text: playingTrack.title || "选择一首，开始聆听"; color: root.ink; font.pixelSize: 14; elide: Text.ElideRight }
                Text { Layout.fillWidth: true; text: playingTrack.artist || "你的音乐，在这里。"; color: root.muted; font.pixelSize: 12; elide: Text.ElideRight }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.maximumWidth: 600; spacing: 2
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter; spacing: 18
                    Action { text: ["顺序", "循环", "单曲", "随机"][queueModel.playbackMode]; ToolTip.text: "切换播放模式"; onClicked: queueModel.playbackMode = (queueModel.playbackMode + 1) % 4 }
                    Action { text: ""; glyph: "|‹"; Accessible.name: "上一首"; ToolTip.text: "上一首"; enabled: queueModel.count > 0; onClicked: playAvailable(-1, true) }
                    Action { text: playerController.playing ? "暂停" : "播放"; primary: true; enabled: playerController.source.length > 0 || queueModel.count > 0; onClicked: togglePlayback() }
                    Action { text: ""; glyph: "›|"; Accessible.name: "下一首"; ToolTip.text: "下一首"; enabled: queueModel.count > 0; onClicked: playAvailable(1, true) }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    Text { text: fmt(playerController.position); color: root.muted; font.pixelSize: 10; Layout.preferredWidth: 38 }
                    Slider {
                        id: seekSlider; Layout.fillWidth: true; implicitHeight: 26; from: 0; to: Math.max(1, playerController.duration)
                        enabled: playerController.duration > 0; Accessible.name: "播放进度"
                        Binding { target: seekSlider; property: "value"; value: playerController.position; when: !seekSlider.pressed; restoreMode: Binding.RestoreNone }
                        onMoved: if (!pressed) playerController.seek(value)
                        onPressedChanged: if (!pressed) playerController.seek(value)
                        ToolTip.visible: pressed; ToolTip.text: fmt(value)
                    }
                    Text { text: fmt(playerController.duration); color: root.muted; font.pixelSize: 10; Layout.preferredWidth: 38 }
                }
            }
            Item { Layout.fillWidth: true; Layout.maximumWidth: 40 }
            RowLayout {
                spacing: 4
                Text { text: "音量"; color: root.muted; font.pixelSize: 11; visible: root.width > 1100 }
                Slider { Layout.preferredWidth: root.width > 1100 ? 90 : 64; from: 0; to: 1; value: playerController.volume; onMoved: playerController.volume = value; Accessible.name: "音量" }
                Action { text: "歌词"; selected: showLyrics; onClicked: showLyrics = !showLyrics }
                Action { text: "队列"; selected: currentTab === 2; onClicked: currentTab = 2 }
            }
        }
    }
}
