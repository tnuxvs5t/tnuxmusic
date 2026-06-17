import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1440
    height: 900
    visible: true
    title: "tnuxmusic"

    Material.theme: Material.Dark
    Material.accent: "#5af2e8"

    property int currentTab: 0
    property int selectedIndex: -1
    property int selectedAlbum: -1
    property string statusText: libraryManager.lastMessage

    readonly property color bg0: "#071014"
    readonly property color bg1: "#0d1b22"
    readonly property color card: "#121f26"
    readonly property color card2: "#182a33"
    readonly property color stroke: "#263d46"
    readonly property color accent: "#5af2e8"
    readonly property color accent2: "#8f7cff"
    readonly property color text0: "#f4fbfc"
    readonly property color text1: "#b5c5ca"
    readonly property color text2: "#758b93"

    function fmt(ms) {
        if (ms < 0 || isNaN(ms)) return "00:00"
        var s = Math.floor(ms / 1000)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        s = s % 60
        var mm = (m < 10 ? "0" + m : "" + m)
        var ss = (s < 10 ? "0" + s : "" + s)
        return h > 0 ? h + ":" + mm + ":" + ss : mm + ":" + ss
    }

    function say(message) {
        statusText = message
    }

    function selectedTrack() {
        return selectedIndex >= 0 ? libraryManager.track(selectedIndex) : ({})
    }

    function selectedAlbumInfo() {
        var row = albumModel.firstTrackRow(selectedAlbum)
        return row >= 0 ? libraryManager.track(row) : ({})
    }

    function startRow(row, touchQueue) {
        if (row < 0) return
        var path = libraryManager.primaryPath(row)
        if (path.length === 0) {
            say("这首歌没有可播放文件")
            return
        }
        selectedIndex = row
        if (touchQueue) queueModel.playNowRow(row)
        playerController.playFile(path)
        say(lyricModel.loadFromFile(libraryManager.lyricPath(row)))
    }

    function playQueueIndex(index) {
        var row = queueModel.activate(index)
        startRow(row, false)
    }

    function enqueueAlbum(albumIndex) {
        var rows = albumModel.tracks(albumIndex)
        for (var i = 0; i < rows.length; ++i)
            queueModel.enqueueRow(rows[i].libraryRow)
        say("已加入专辑到队列：" + rows.length + " 首")
    }

    function playAlbum(albumIndex) {
        queueModel.clear()
        enqueueAlbum(albumIndex)
        var row = queueModel.activate(0)
        startRow(row, false)
    }

    function playlistNameOrCurrent() {
        if (playlistName.text.trim().length > 0) return playlistName.text.trim()
        return playlistBox.currentText
    }

    Connections {
        target: playerController
        function onFinished() {
            var row = queueModel.next()
            if (row >= 0) startRow(row, false)
        }
    }

    FolderDialog {
        id: scanDialog
        title: "选择音乐文件夹"
        onAccepted: say(libraryManager.scanFolder(selectedFolder))
    }

    FileDialog {
        id: importDialog
        title: "导入曲库 JSON"
        fileMode: FileDialog.OpenFile
        nameFilters: ["TnuxMusic Library (*.json)", "JSON (*.json)", "All files (*)"]
        onAccepted: say(libraryManager.importLibrary(selectedFile))
    }

    FileDialog {
        id: mergeDialog
        title: "合并曲库 JSON"
        fileMode: FileDialog.OpenFile
        nameFilters: ["TnuxMusic Library (*.json)", "JSON (*.json)", "All files (*)"]
        onAccepted: say(libraryManager.mergeLibrary(selectedFile))
    }

    FileDialog {
        id: exportDialog
        title: "导出曲库 JSON"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["TnuxMusic Library (*.json)", "JSON (*.json)", "All files (*)"]
        onAccepted: say(libraryManager.exportLibrary(selectedFile))
    }

    FileDialog {
        id: scriptDialog
        title: "运行曲库整理 JS"
        fileMode: FileDialog.OpenFile
        nameFilters: ["JavaScript (*.js)", "All files (*)"]
        onAccepted: say(scriptBridge.runScript(selectedFile))
    }

    component AccentButton: Button {
        id: btn
        padding: 10
        leftPadding: 16
        rightPadding: 16
        contentItem: Text {
            text: btn.text
            color: btn.enabled ? root.text0 : root.text2
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 14
            color: btn.down ? "#36bdb5" : (btn.hovered ? "#49ded4" : root.accent)
            opacity: btn.enabled ? 1.0 : 0.35
        }
    }

    component GhostButton: Button {
        id: btn
        property bool selected: false
        padding: 10
        leftPadding: 14
        rightPadding: 14
        contentItem: Text {
            text: btn.text
            color: btn.enabled ? root.text1 : root.text2
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 14
            color: btn.selected ? "#1f3b45" : (btn.down ? "#1f3944" : (btn.hovered ? "#1a3039" : "#101c22"))
            border.color: root.stroke
            opacity: btn.enabled ? 1.0 : 0.35
        }
    }

    component Card: Rectangle {
        radius: 22
        color: root.card
        border.color: root.stroke
        border.width: 1
        layer.enabled: true
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.bg1 }
            GradientStop { position: 1.0; color: root.bg0 }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Rectangle {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                radius: 16
                gradient: Gradient {
                    GradientStop { position: 0; color: root.accent }
                    GradientStop { position: 1; color: root.accent2 }
                }
                Text {
                    anchors.centerIn: parent
                    text: "♪"
                    font.pixelSize: 28
                    font.bold: true
                    color: "#071014"
                }
            }

            ColumnLayout {
                spacing: 0
                Label { text: "tnuxmusic"; color: root.text0; font.pixelSize: 26; font.bold: true }
                Label { text: "音乐平台 · 播放器 · TLY 动态歌词"; color: root.text2; font.pixelSize: 13 }
            }

            Item { Layout.fillWidth: true }

            GhostButton { text: "扫描音乐"; onClicked: scanDialog.open() }
            GhostButton { text: "导入"; onClicked: importDialog.open() }
            GhostButton { text: "合并"; onClicked: mergeDialog.open() }
            GhostButton { text: "导出"; onClicked: exportDialog.open() }
            AccentButton { text: "JS 整理"; onClicked: scriptDialog.open() }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            Card {
                Layout.preferredWidth: 230
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14

                    Label { text: "导航"; color: root.text2; font.pixelSize: 12; font.bold: true }

                    GhostButton {
                        text: "曲库  " + libraryManager.count
                        Layout.fillWidth: true
                        onClicked: currentTab = 0
                        selected: currentTab === 0
                    }
                    GhostButton {
                        text: "专辑墙  " + albumModel.count
                        Layout.fillWidth: true
                        onClicked: currentTab = 1
                        selected: currentTab === 1
                    }
                    GhostButton {
                        text: "队列 / 歌单  " + queueModel.count
                        Layout.fillWidth: true
                        onClicked: currentTab = 2
                        selected: currentTab === 2
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: root.stroke }

                    Label { text: "状态"; color: root.text2; font.pixelSize: 12; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        text: playerController.errorText.length > 0 ? playerController.errorText : statusText
                        color: playerController.errorText.length > 0 ? "#ff8a8a" : root.text1
                        wrapMode: Text.WordWrap
                    }

                    Item { Layout.fillHeight: true }

                    Label {
                        Layout.fillWidth: true
                        text: "默认库\n" + libraryManager.libraryPath
                        color: root.text2
                        font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }

            StackLayout {
                id: pages
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: currentTab

                Card {
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: search
                                Layout.fillWidth: true
                                placeholderText: "搜索标题 / 艺术家 / 专辑 / 风格"
                                selectByMouse: true
                            }
                            GhostButton { text: "清空"; onClicked: search.clear() }
                        }

                        ListView {
                            id: trackList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: libraryManager
                            spacing: 8
                            delegate: Rectangle {
                                width: trackList.width
                                height: visible ? 84 : 0
                                radius: 18
                                color: selectedIndex === index ? "#1d3d45" : (mouse.containsMouse ? "#172932" : "#0e181e")
                                border.color: selectedIndex === index ? root.accent : root.stroke
                                visible: {
                                    var q = search.text.toLowerCase()
                                    if (q.length === 0) return true
                                    return (title + " " + artist + " " + album + " " + genre).toLowerCase().indexOf(q) >= 0
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 12
                                    Rectangle {
                                        Layout.preferredWidth: 58
                                        Layout.preferredHeight: 58
                                        radius: 14
                                        color: "#1e3038"
                                        clip: true
                                        Image { anchors.fill: parent; source: coverUrl; fillMode: Image.PreserveAspectCrop; visible: coverUrl.length > 0 }
                                        Text { anchors.centerIn: parent; text: "♪"; color: root.accent; font.pixelSize: 24; font.bold: true; visible: coverUrl.length === 0 }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Label { text: title; color: root.text0; font.pixelSize: 16; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Label { text: (artist.length ? artist : "未知艺术家") + " · " + (album.length ? album : "未知专辑"); color: root.text1; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Label { text: qualitiesText; color: root.text2; elide: Text.ElideRight; Layout.fillWidth: true }
                                    }
                                    GhostButton { text: "+队列"; onClicked: say("队列位置 #" + (queueModel.enqueueRow(index) + 1)) }
                                    AccentButton { text: "播放"; onClicked: startRow(index, true) }
                                }

                                MouseArea {
                                    id: mouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.LeftButton
                                    onClicked: selectedIndex = index
                                    onDoubleClicked: startRow(index, true)
                                }
                            }
                        }
                    }
                }

                Card {
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 16

                        GridView {
                            id: albumGrid
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            cellWidth: 190
                            cellHeight: 250
                            model: albumModel
                            delegate: Rectangle {
                                width: 172
                                height: 232
                                radius: 22
                                color: selectedAlbum === index ? "#1d3d45" : (albumMouse.containsMouse ? "#172932" : "#0e181e")
                                border.color: selectedAlbum === index ? root.accent : root.stroke

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 8
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 148
                                        radius: 18
                                        color: "#1e3038"
                                        clip: true
                                        Image { anchors.fill: parent; source: coverUrl; fillMode: Image.PreserveAspectCrop; visible: coverUrl.length > 0 }
                                        Text { anchors.centerIn: parent; text: "ALBUM"; color: root.accent; font.bold: true; visible: coverUrl.length === 0 }
                                    }
                                    Label { Layout.fillWidth: true; text: album; color: root.text0; font.bold: true; elide: Text.ElideRight }
                                    Label { Layout.fillWidth: true; text: subtitle; color: root.text2; elide: Text.ElideRight }
                                }
                                MouseArea {
                                    id: albumMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: selectedAlbum = index
                                    onDoubleClicked: playAlbum(index)
                                }
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 360
                            Layout.fillHeight: true
                            radius: 22
                            color: "#0e181e"
                            border.color: root.stroke

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 12

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 300
                                    radius: 22
                                    color: "#1e3038"
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        source: selectedAlbumInfo().coverUrl || ""
                                        fillMode: Image.PreserveAspectCrop
                                        visible: source.toString().length > 0
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        text: "选择专辑"
                                        color: root.text2
                                        visible: !(selectedAlbumInfo().coverUrl || "").length
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: selectedAlbumInfo().album || "专辑详情"
                                    color: root.text0
                                    font.pixelSize: 22
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: selectedAlbumInfo().artist || "从左侧专辑墙选择"
                                    color: root.text1
                                    elide: Text.ElideRight
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    AccentButton { text: "播放专辑"; enabled: selectedAlbum >= 0; onClicked: playAlbum(selectedAlbum) }
                                    GhostButton { text: "加入队列"; enabled: selectedAlbum >= 0; onClicked: enqueueAlbum(selectedAlbum) }
                                }

                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    spacing: 6
                                    model: selectedAlbum >= 0 ? albumModel.tracks(selectedAlbum) : []
                                    delegate: Rectangle {
                                        width: parent ? parent.width : 320
                                        height: 50
                                        radius: 12
                                        color: albumTrackMouse.containsMouse ? "#172932" : "transparent"
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            Label { text: modelData.track > 0 ? modelData.track : index + 1; color: root.text2; Layout.preferredWidth: 30 }
                                            Label { text: modelData.title; color: root.text0; elide: Text.ElideRight; Layout.fillWidth: true }
                                            GhostButton { text: "+"; onClicked: queueModel.enqueueRow(modelData.libraryRow) }
                                        }
                                        MouseArea {
                                            id: albumTrackMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onDoubleClicked: startRow(modelData.libraryRow, true)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Card {
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 16

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 12

                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "播放队列"; color: root.text0; font.pixelSize: 24; font.bold: true }
                                Item { Layout.fillWidth: true }
                                GhostButton { text: "上一首"; enabled: queueModel.count > 0; onClicked: startRow(queueModel.previous(), false) }
                                AccentButton { text: "下一首"; enabled: queueModel.count > 0; onClicked: startRow(queueModel.next(), false) }
                                GhostButton { text: "清空"; onClicked: queueModel.clear() }
                            }

                            ListView {
                                id: queueList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: queueModel
                                spacing: 8
                                delegate: Rectangle {
                                    width: queueList.width
                                    height: 70
                                    radius: 18
                                    color: active ? "#1d3d45" : (queueMouse.containsMouse ? "#172932" : "#0e181e")
                                    border.color: active ? root.accent : root.stroke
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        Label { text: "#" + (queueIndex + 1); color: active ? root.accent : root.text2; Layout.preferredWidth: 46 }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Label { text: title || "曲目丢失"; color: root.text0; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                            Label { text: (artist || "未知艺术家") + " · " + (album || "未知专辑"); color: root.text2; elide: Text.ElideRight; Layout.fillWidth: true }
                                        }
                                        GhostButton { text: "移除"; onClicked: queueModel.removeAt(queueIndex) }
                                    }
                                    MouseArea {
                                        id: queueMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onDoubleClicked: playQueueIndex(queueIndex)
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 340
                            Layout.fillHeight: true
                            radius: 22
                            color: "#0e181e"
                            border.color: root.stroke

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 12

                                Label { text: "歌单"; color: root.text0; font.pixelSize: 24; font.bold: true }
                                ComboBox {
                                    id: playlistBox
                                    Layout.fillWidth: true
                                    model: queueModel.playlistNames
                                }
                                TextField {
                                    id: playlistName
                                    Layout.fillWidth: true
                                    placeholderText: "新歌单名 / 或使用上方选择"
                                    selectByMouse: true
                                }
                                AccentButton {
                                    Layout.fillWidth: true
                                    text: "保存当前队列为歌单"
                                    onClicked: say(queueModel.saveQueueAsPlaylist(playlistNameOrCurrent()))
                                }
                                GhostButton {
                                    Layout.fillWidth: true
                                    text: "加载歌单"
                                    onClicked: say(queueModel.loadPlaylist(playlistNameOrCurrent(), true))
                                }
                                GhostButton {
                                    Layout.fillWidth: true
                                    text: "追加歌单到队列"
                                    onClicked: say(queueModel.loadPlaylist(playlistNameOrCurrent(), false))
                                }
                                GhostButton {
                                    Layout.fillWidth: true
                                    text: "创建空歌单"
                                    onClicked: say(queueModel.createPlaylist(playlistName.text))
                                }
                                GhostButton {
                                    Layout.fillWidth: true
                                    text: "删除歌单"
                                    onClicked: say(queueModel.deletePlaylist(playlistNameOrCurrent()))
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: "提示：在曲库里点 +队列，再保存为歌单；或先选中歌单名，后续可扩展为右键加入。"
                                    color: root.text2
                                    wrapMode: Text.WordWrap
                                }
                                Item { Layout.fillHeight: true }
                            }
                        }
                    }
                }
            }

            Card {
                Layout.preferredWidth: 410
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 320
                        radius: 28
                        color: "#1e3038"
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: selectedTrack().coverUrl || ""
                            fillMode: Image.PreserveAspectCrop
                            visible: source.toString().length > 0
                        }
                        Rectangle {
                            anchors.fill: parent
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#00000000" }
                                GradientStop { position: 1.0; color: "#aa000000" }
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            text: "TNX\nMUSIC"
                            horizontalAlignment: Text.AlignHCenter
                            color: root.accent
                            font.pixelSize: 36
                            font.bold: true
                            visible: !(selectedTrack().coverUrl || "").length
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: selectedTrack().title || "选择一首歌开始"
                        color: root.text0
                        font.pixelSize: 24
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: (selectedTrack().artist || "未知艺术家") + " · " + (selectedTrack().album || "未知专辑")
                        color: root.text1
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: selectedTrack().qualitiesText || ""
                        color: root.text2
                        elide: Text.ElideRight
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, playerController.duration)
                        value: playerController.position
                        onMoved: playerController.seek(value)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: fmt(playerController.position); color: root.text2 }
                        Item { Layout.fillWidth: true }
                        Label { text: fmt(playerController.duration); color: root.text2 }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        GhostButton { text: "⏮"; enabled: queueModel.count > 0; onClicked: startRow(queueModel.previous(), false) }
                        AccentButton {
                            text: playerController.playing ? "暂停" : "播放"
                            Layout.fillWidth: true
                            enabled: selectedIndex >= 0 || playerController.source.length > 0
                            onClicked: {
                                if (playerController.source.length === 0 && selectedIndex >= 0) startRow(selectedIndex, true)
                                else playerController.toggle()
                            }
                        }
                        GhostButton { text: "⏭"; enabled: queueModel.count > 0; onClicked: startRow(queueModel.next(), false) }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "音量"; color: root.text2 }
                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            value: playerController.volume
                            onMoved: playerController.volume = value
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: root.stroke }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "TLY 歌词"; color: root.accent; font.pixelSize: 18; font.bold: true }
                        Item { Layout.fillWidth: true }
                        GhostButton { text: "重载"; enabled: selectedIndex >= 0; onClicked: say(lyricModel.loadFromFile(libraryManager.lyricPath(selectedIndex))) }
                    }

                    ListView {
                        id: lyricList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: lyricModel
                        delegate: Rectangle {
                            width: lyricList.width
                            height: Math.max(72, lyricColumn.implicitHeight + 20)
                            radius: 16
                            color: active ? "#1d3d45" : "transparent"
                            border.color: active ? root.accent : "transparent"
                            ColumnLayout {
                                id: lyricColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: 10
                                spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: timeText; color: active ? root.accent : root.text2; font.family: "monospace" }
                                    ProgressBar {
                                        Layout.fillWidth: true
                                        from: 0
                                        to: 1
                                        value: progress
                                        visible: active
                                    }
                                }
                                Text {
                                    text: richText
                                    textFormat: Text.RichText
                                    color: root.text0
                                    font.pixelSize: active ? 23 : 18
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: translation
                                    color: active ? "#bffaf6" : root.text2
                                    font.pixelSize: 14
                                    visible: translation.length > 0
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        Connections {
                            target: lyricModel
                            function onActiveIndexChanged() {
                                if (lyricModel.activeIndex >= 0)
                                    lyricList.positionViewAtIndex(lyricModel.activeIndex, ListView.Center)
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            width: parent.width - 40
                            horizontalAlignment: Text.AlignHCenter
                            text: "没有歌词。把同名 .tly 放在音频旁边，扫描后会自动关联。"
                            color: root.text2
                            wrapMode: Text.WordWrap
                            visible: lyricList.count === 0
                        }
                    }
                }
            }
        }
    }
}
