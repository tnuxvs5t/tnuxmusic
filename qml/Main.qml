import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1320
    height: 820
    visible: true
    title: "tnuxmusic"

    Material.theme: Material.Dark
    Material.accent: "#48d1cc"

    property int selectedIndex: -1
    property string statusText: libraryManager.lastMessage

    function fmt(ms) {
        if (ms < 0 || isNaN(ms)) return "00:00"
        var s = Math.floor(ms / 1000)
        var m = Math.floor(s / 60)
        s = s % 60
        return (m < 10 ? "0" + m : "" + m) + ":" + (s < 10 ? "0" + s : "" + s)
    }

    function say(message) {
        statusText = message
    }

    function selectedTrack() {
        return selectedIndex >= 0 ? libraryManager.track(selectedIndex) : ({})
    }

    function playIndex(i) {
        selectedIndex = i
        var path = libraryManager.primaryPath(i)
        if (path.length === 0) {
            say("这首歌没有可播放文件")
            return
        }
        playerController.playFile(path)
        say(lyricModel.loadFromFile(libraryManager.lyricPath(i)))
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

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8

            Label {
                text: "tnuxmusic"
                font.pixelSize: 22
                font.bold: true
                color: "#48d1cc"
            }
            Label {
                text: "曲库 " + libraryManager.count + " 首"
                color: "#b9c4c8"
            }
            Item { Layout.fillWidth: true }
            Button { text: "扫描音乐"; onClicked: scanDialog.open() }
            Button { text: "导入"; onClicked: importDialog.open() }
            Button { text: "合并"; onClicked: mergeDialog.open() }
            Button { text: "导出"; onClicked: exportDialog.open() }
            Button { text: "JS整理"; onClicked: scriptDialog.open() }
        }
    }

    footer: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 10
            Label {
                text: playerController.errorText.length > 0 ? playerController.errorText : statusText
                color: playerController.errorText.length > 0 ? "#ff8080" : "#c8d1d3"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label { text: "默认库：" + libraryManager.libraryPath; color: "#77868a"; elide: Text.ElideMiddle; Layout.preferredWidth: 420 }
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Pane {
            SplitView.preferredWidth: 430
            padding: 12
            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                TextField {
                    id: search
                    Layout.fillWidth: true
                    placeholderText: "搜索标题 / 艺术家 / 专辑"
                    selectByMouse: true
                }

                ListView {
                    id: trackList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: libraryManager
                    spacing: 6
                    delegate: Rectangle {
                        width: trackList.width
                        height: visible ? 72 : 0
                        radius: 10
                        color: selectedIndex === index ? "#263f43" : (mouse.containsMouse ? "#202a2d" : "#151b1e")
                        border.color: selectedIndex === index ? "#48d1cc" : "#283236"
                        visible: {
                            var q = search.text.toLowerCase()
                            if (q.length === 0) return true
                            return (title + " " + artist + " " + album).toLowerCase().indexOf(q) >= 0
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 10
                            Rectangle {
                                Layout.preferredWidth: 50
                                Layout.preferredHeight: 50
                                radius: 8
                                color: "#283236"
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    source: coverUrl
                                    fillMode: Image.PreserveAspectCrop
                                    visible: coverUrl.length > 0
                                }
                                Label {
                                    anchors.centerIn: parent
                                    text: "♪"
                                    font.pixelSize: 24
                                    color: "#48d1cc"
                                    visible: coverUrl.length === 0
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label { text: title; color: "#eff7f8"; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: (artist.length ? artist : "未知艺术家") + " · " + (album.length ? album : "未知专辑"); color: "#aab7ba"; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: qualityCount + " 音质：" + qualitiesText; color: "#6f858a"; elide: Text.ElideRight; Layout.fillWidth: true }
                            }
                        }

                        MouseArea {
                            id: mouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: selectedIndex = index
                            onDoubleClicked: playIndex(index)
                        }
                    }
                }
            }
        }

        Pane {
            SplitView.fillWidth: true
            padding: 20

            ColumnLayout {
                anchors.fill: parent
                spacing: 16

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 22

                    Rectangle {
                        Layout.preferredWidth: 250
                        Layout.preferredHeight: 250
                        radius: 18
                        color: "#1a2225"
                        border.color: "#304044"
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: selectedTrack().coverUrl || ""
                            fillMode: Image.PreserveAspectCrop
                            visible: source.toString().length > 0
                        }
                        Label {
                            anchors.centerIn: parent
                            text: "TNX\nMUSIC"
                            horizontalAlignment: Text.AlignHCenter
                            color: "#48d1cc"
                            font.pixelSize: 32
                            font.bold: true
                            visible: !(selectedTrack().coverUrl || "").length
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 8
                        Label {
                            text: selectedTrack().title || "选择一首歌开始"
                            color: "#f5fbfc"
                            font.pixelSize: 32
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: (selectedTrack().artist || "未知艺术家") + " · " + (selectedTrack().album || "未知专辑")
                            color: "#aab7ba"
                            font.pixelSize: 18
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: selectedTrack().qualitiesText || ""
                            color: "#6f858a"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            spacing: 12
                            Button {
                                text: playerController.playing ? "暂停" : "播放"
                                enabled: selectedIndex >= 0 || playerController.source.length > 0
                                onClicked: {
                                    if (playerController.source.length === 0 && selectedIndex >= 0) playIndex(selectedIndex)
                                    else playerController.toggle()
                                }
                            }
                            Button { text: "停止"; onClicked: playerController.stop() }
                            Button {
                                text: "加载歌词"
                                enabled: selectedIndex >= 0
                                onClicked: say(lyricModel.loadFromFile(libraryManager.lyricPath(selectedIndex)))
                            }
                            Label { text: fmt(playerController.position) + " / " + fmt(playerController.duration); color: "#c8d1d3" }
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: Math.max(1, playerController.duration)
                            value: playerController.position
                            onMoved: playerController.seek(value)
                        }

                        RowLayout {
                            Label { text: "音量"; color: "#aab7ba" }
                            Slider {
                                Layout.preferredWidth: 180
                                from: 0
                                to: 1
                                value: playerController.volume
                                onMoved: playerController.volume = value
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: "#283236"
                }

                Label {
                    text: "TLY 动态歌词"
                    color: "#48d1cc"
                    font.pixelSize: 20
                    font.bold: true
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
                        height: Math.max(64, lyricColumn.implicitHeight + 18)
                        radius: 10
                        color: active ? "#234649" : "transparent"
                        border.color: active ? "#48d1cc" : "transparent"

                        ColumnLayout {
                            id: lyricColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 10
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: timeText; color: active ? "#9ff4ef" : "#6f858a"; font.family: "monospace" }
                                Label { text: tags; color: "#6f858a"; visible: tags.length > 0; elide: Text.ElideRight; Layout.fillWidth: true }
                            }
                            Label {
                                text: text
                                color: active ? "#ffffff" : "#d8e1e3"
                                font.pixelSize: active ? 24 : 19
                                font.bold: active
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Label {
                                text: translation
                                color: active ? "#bffaf6" : "#8da0a5"
                                font.pixelSize: 15
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
                        text: "没有歌词。把同名 .tly 放在音频旁边，扫描后会自动关联。"
                        color: "#6f858a"
                        visible: lyricList.count === 0
                    }
                }
            }
        }
    }
}

