#include "AlbumModel.h"
#include "LibraryManager.h"
#include "MetadataReader.h"
#include "PlaybackQueue.h"
#include "PlayerController.h"
#include "LyricModel.h"
#include "ScriptBridge.h"
#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtEndian>

static Track song(QString id, QString artist, QString album, QString path, int no = 1)
{
    Track t;
    t.id = id; t.title = "Song " + id; t.artist = artist; t.album = album; t.trackNo = no;
    t.qualities.append({"MP3", path, "MP3"});
    return t;
}
static QJsonObject library(const QVector<Track> &tracks)
{
    QJsonArray array;
    for (const auto &t : tracks) array.append(t.toJson());
    return {{"schema", "tnuxmusic.library.v1"}, {"tracks", array}};
}
static void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes), bytes.size());
}
static QByteArray readFile(const QString &path)
{
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}

class RegressionTests : public QObject {
    Q_OBJECT
private slots:
    void init() {
        // The executable sets XDG paths to its private QTemporaryDir before Qt starts.
        const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QVERIFY(root.contains("tnuxmusic-test-"));
        QDir(root).removeRecursively();
        QSettings().clear();
    }
    void relativePathsUseManifest() {
        QTemporaryDir a, b;
        writeFile(a.filePath("song.mp3"), "a"); writeFile(b.filePath("song.mp3"), "b");
        const QString previous = QDir::currentPath();
        QDir::setCurrent(a.path());
        const QString actual = canonicalLocalPath("song.mp3", b.path());
        QDir::setCurrent(previous);
        QCOMPARE(actual, b.filePath("song.mp3"));
    }
    void invalidImportPreservesLibrary() {
        LibraryManager model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(model.replaceFromJsonObject(library({song("1", "A", "Album", "/tmp/1.mp3")})));
        const auto before = model.toJsonObject();
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QString error;
        QVERIFY(!model.replaceFromJsonObject({{"hello", "world"}}, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(model.toJsonObject(), before);
        auto duplicate = song("1", "B", "B", "/tmp/2.mp3");
        QVERIFY(!model.replaceFromJsonObject(library({song("1", "A", "Album", "/tmp/1.mp3"), duplicate}), &error));
        QCOMPARE(resets.count(), 0);
        QCOMPARE(model.count(), 1);
    }
    void discIdentityAndSingleReset() {
        LibraryManager model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto a = song("1", "A", "Album", "/tmp/disc1.mp3"); a.title = "Intro";
        auto b = song("2", "A", "Album", "/tmp/disc2.mp3"); b.title = "Intro"; b.disc = 2;
        QVERIFY(model.replaceFromJsonObject(library({a})));
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QVERIFY(model.mergeFromJsonObject(library({b})));
        QCOMPARE(model.count(), 2); QCOMPARE(resets.count(), 1);
        QVERIFY(model.mergeFromJsonObject(library({b})));
        QCOMPARE(model.count(), 2); QCOMPARE(model.trackAt(model.rowOfId("2"))->qualities.size(), 1);
    }
    void groupingDoesNotDependOnArtwork() {
        LibraryManager model;
        AlbumModel albums(&model);
        QAbstractItemModelTester tester(&albums, QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto a = song("1", "Singer A", "Compilation", "/tmp/shared/1.mp3", 2); a.albumArtist = "Label"; a.coverPath = "/missing/red.png";
        auto b = song("2", "Singer B", "Compilation", "/tmp/other/2.mp3", 1); b.albumArtist = "Label";
        auto c = song("3", "Other", "Compilation", "/tmp/shared/3.mp3"); c.albumArtist = "Other Label"; c.coverPath = a.coverPath;
        QVERIFY(model.replaceFromJsonObject(library({a,b,c})));
        QCOMPARE(albums.count(), 2);
        const auto choices = albums.choices();
        QString key;
        for (const auto &item : choices) if (item.toMap().value("count").toInt() == 2) key = item.toMap().value("key").toString();
        const auto tracks = albums.tracks(albums.indexOfKey(key));
        QCOMPARE(tracks.first().toMap().value("id").toString(), "2");
        albums.setAutoMergeAlbums(true); QCOMPARE(albums.count(), 2);
        albums.setSearchQuery("other label"); QCOMPARE(albums.rowCount(), 1); QCOMPARE(albums.count(), 2);
    }
    void directoryGroupingIsConservative() {
        LibraryManager model; AlbumModel albums(&model);
        auto a = song("1", "A", "Compilation", "/tmp/disc/1.mp3");
        auto b = song("2", "B", "Compilation", "/tmp/disc/2.mp3");
        auto c = song("3", "C", "Compilation", "/tmp/elsewhere/3.mp3");
        QVERIFY(model.replaceFromJsonObject(library({a,b,c})));
        QCOMPARE(albums.count(), 3); albums.setAutoMergeAlbums(true); QCOMPARE(albums.count(), 2);
        albums.setAutoMergeAlbums(false); QCOMPARE(albums.count(), 3);
    }
    void explicitMergeRetainsIdsAndPreciseDeletion() {
        LibraryManager model; AlbumModel albums(&model); PlaybackQueue queue(&model);
        auto a = song("a", "A", "First", "/tmp/a.mp3");
        auto b = song("b", "B", "Second", "/tmp/b.mp3");
        auto c = song("c", "C", "Second", "/tmp/c.mp3");
        QVERIFY(model.replaceFromJsonObject(library({a,b,c})));
        queue.enqueueRow(model.rowOfId("a")); queue.enqueueRow(model.rowOfId("b")); queue.activate(1);
        QString source, target;
        for (const auto &v : albums.choices()) {
            const QString key = v.toMap().value("key").toString();
            if (albums.info(key).value("artist") == "A") source = key;
            if (albums.info(key).value("artist") == "B") target = key;
        }
        QVERIFY(albums.mergeAlbums(source,target).startsWith("已保存"));
        QCOMPARE(model.count(),3); QCOMPARE(albums.count(),2); QCOMPARE(queue.currentRow(),model.rowOfId("b"));
        QCOMPARE(model.trackAt(model.rowOfId("a"))->artist, "A");
        const QString group = model.trackAt(model.rowOfId("a"))->albumId;
        QVERIFY(!group.isEmpty());
        // A rescan keeps explicit grouping despite the original album tag.
        QVERIFY(model.mergeFromJsonObject(library({a,b})));
        QCOMPARE(model.count(),3); QCOMPARE(model.trackAt(model.rowOfId("a"))->albumId,group);
        LibraryManager reloaded; QVERIFY(reloaded.replaceFromJsonObject(model.toJsonObject()));
        QCOMPARE(reloaded.count(),3); QCOMPARE(reloaded.trackAt(reloaded.rowOfId("a"))->albumId, group);
        QVERIFY(albums.removeAlbum("id:"+group).startsWith("已从曲库"));
        QCOMPARE(model.count(),1); QVERIFY(model.rowOfId("c")>=0); QCOMPARE(queue.count(),0);
    }
    void persistedDistinctTracksAreNotCollapsed() {
        LibraryManager model;
        auto a=song("a","A","Album","/tmp/a.mp3"), b=song("b","A","Album","/tmp/b.mp3");
        a.title=b.title="Intro"; a.albumId=b.albumId="manual-group";
        QVERIFY(model.replaceFromJsonObject(library({a,b})));
        QCOMPARE(model.count(),2);
        QVERIFY(model.rowOfId("a")!=model.rowOfId("b"));
    }
    void atomicSaveFailureKeepsState() {
        LibraryManager model;
        QVERIFY(model.replaceFromJsonObject(library({song("a","A","Album","/tmp/a.mp3")})));
        // A directory at the destination makes atomic replacement fail.
        QVERIFY(QDir().mkpath(model.libraryPath()));
        const auto before=model.toJsonObject();
        QVERIFY(!model.clearLibrary().startsWith("已"));
        QCOMPARE(model.toJsonObject(),before);
        QVERIFY(!model.updateAlbum({"a"},"Changed","A",2020,"manual").startsWith("已"));
        QCOMPARE(model.toJsonObject(),before);
    }
    void asyncImportResponsiveAndTransactional() {
        QTemporaryDir temp;
        LibraryManager model;
        QVector<Track> tracks;
        for (int i=0;i<8000;++i) tracks.append(song(QString::number(i),"Artist",QString("Album %1").arg(i/10),temp.filePath(QString::number(i)+".mp3"),i%10));
        const QString source=temp.filePath("library.json"); writeFile(source,QJsonDocument(library(tracks)).toJson());
        QSignalSpy done(&model,&LibraryManager::operationFinished);
        int ticks=0; QTimer pulse; pulse.setInterval(1); connect(&pulse,&QTimer::timeout,[&]{++ticks;}); pulse.start();
        QVERIFY(model.startOperation("import",source)); QVERIFY(model.busy());
        QVERIFY(!model.startOperation("scan",temp.path()));
        QVERIFY(!model.clearLibrary().startsWith("已"));
        QVERIFY(done.wait(20000)); QVERIFY(done.first().first().toBool());
        QVERIFY(ticks>0); QCOMPARE(model.count(),8000); QVERIFY(!model.busy());
        auto before=model.toJsonObject();
        writeFile(source,"{\"wrong\":true}"); done.clear();
        QVERIFY(model.startOperation("import",source)); QVERIFY(done.wait(5000));
        QVERIFY(!done.first().first().toBool()); QCOMPARE(model.toJsonObject(),before);
        // A persistence failure must not publish the worker's modified snapshot.
        QVERIFY(QFile::remove(model.libraryPath())); QVERIFY(QDir().mkpath(model.libraryPath()));
        writeFile(source,QJsonDocument(library({song("new","New","New",temp.filePath("new.mp3"))})).toJson());
        done.clear(); QVERIFY(model.startOperation("import",source)); QVERIFY(done.wait(5000));
        QVERIFY(!done.first().first().toBool()); QCOMPARE(model.toJsonObject(),before);
    }
    void localizedZipRoundTripAndCorruption() {
        QTemporaryDir temp;
        const QString audio=temp.filePath("source.mp3"); writeFile(audio,"fake-audio-contents");
        auto t=song("a","Singer","Album",audio); t.albumArtist="Label"; t.albumId="manual";
        t.coverPath=temp.filePath("missing.jpg"); t.lyricPath=temp.filePath("missing.tly");
        LibraryManager model; QVERIFY(model.replaceFromJsonObject(library({t})));
        const QString zip=temp.filePath("music.zip"); QVERIFY(model.exportLocalizedZip(zip).startsWith("已本地化"));
        LibraryManager imported; QVERIFY(imported.importLibrary(zip).startsWith("已导入"));
        QCOMPARE(imported.count(),1); QCOMPARE(readFile(imported.primaryPath(0)),readFile(audio));
        QCOMPARE(imported.trackAt(0)->albumId,"manual"); QVERIFY(imported.trackAt(0)->coverPath.isEmpty()); QVERIFY(imported.trackAt(0)->lyricPath.isEmpty());
        QVERIFY(imported.mergeLibrary(zip).startsWith("已合并")); QCOMPARE(imported.count(),1); QCOMPARE(imported.trackAt(0)->qualities.size(),1);
        auto bytes=readFile(zip); const auto location=bytes.indexOf("fake-audio-contents"); QVERIFY(location>=0); bytes[location]='X';
        const QString corrupt=temp.filePath("corrupt.zip"); writeFile(corrupt,bytes);
        const auto before=imported.toJsonObject();
        QVERIFY(!imported.importLibrary(corrupt).startsWith("已导入")); QCOMPARE(imported.toJsonObject(),before);
    }
    void metadataArtistAndAlbumArtistAreIndependent() {
        QTemporaryDir temp;
        auto frame=[](const QByteArray &id,const QByteArray &text) {
            const QByteArray payload=QByteArray(1,3)+text;
            QByteArray size(4,0); qToBigEndian<quint32>(payload.size(),size.data());
            return id+size+QByteArray(2,0)+payload;
        };
        const QByteArray frames=frame("TPE2","Compilation Label")+frame("TPE1","Individual Singer")+frame("TALB","Compilation");
        QByteArray size(4,0); quint32 length=frames.size();
        for(int i=3;i>=0;--i) {size[i]=char(length&0x7f); length>>=7;}
        const QString audio=temp.filePath("song.mp3");
        writeFile(audio,QByteArray("ID3",3)+QByteArray::fromHex("030000")+size+frames);
        const auto metadata=MetadataReader::read(audio);
        QCOMPARE(metadata.artist,"Individual Singer"); QCOMPARE(metadata.albumArtist,"Compilation Label");
        auto track=song("a","Individual Singer","Compilation",audio);
        LibraryManager model; QVERIFY(model.replaceFromJsonObject(library({track})));
        QVERIFY(model.refreshAlbumMetadata().startsWith("专辑标签补全完成"));
        QCOMPARE(model.trackAt(0)->albumArtist,"Compilation Label"); QCOMPARE(model.trackAt(0)->artist,"Individual Singer");
    }
    void undoRestoresAlbumMetadata() {
        LibraryManager model; AlbumModel albums(&model);
        QVERIFY(model.replaceFromJsonObject(library({song("a","A","First","/tmp/a.mp3"),song("b","B","Second","/tmp/b.mp3")})));
        const auto before=model.toJsonObject();
        auto choices=albums.choices();
        QVERIFY(albums.mergeAlbums(choices[0].toMap().value("key").toString(),choices[1].toMap().value("key").toString()).startsWith("已保存"));
        QCOMPARE(albums.count(),1); QVERIFY(model.canUndoEdit());
        QVERIFY(model.undoLastEdit().startsWith("已撤销")); QCOMPARE(model.toJsonObject(),before); QCOMPARE(albums.count(),2); QVERIFY(!model.canUndoEdit());
    }
    void zipRejectsTraversalAndManifestEscape() {
        QTemporaryDir temp;
        const QString audio=temp.filePath("original.mp3"); writeFile(audio,"contents");
        LibraryManager model; QVERIFY(model.replaceFromJsonObject(library({song("a","A","Album",audio)})));
        const QString good=temp.filePath("good.zip"); QVERIFY(model.exportLocalizedZip(good).startsWith("已本地化"));
        auto bytes=readFile(good);
        // Replace first entry's path with an absolute path of identical length.
        const int nameLength=qFromLittleEndian<quint16>(bytes.constData()+26);
        QVERIFY(nameLength>0); bytes[30]='/';
        const QString malicious=temp.filePath("malicious.zip"); writeFile(malicious,bytes);
        const auto before=model.toJsonObject();
        QVERIFY(!model.importLibrary(malicious).startsWith("已导入"));
        QCOMPARE(model.toJsonObject(),before); QCOMPARE(readFile(audio),QByteArray("contents"));
    }
    void playlistFailureAndActiveRemoval() {
        LibraryManager model; PlaybackQueue queue(&model);
        QVERIFY(model.replaceFromJsonObject(library({song("a","A","A","/tmp/a.mp3"),song("b","B","B","/tmp/b.mp3"),song("c","C","C","/tmp/c.mp3")})));
        QSignalSpy changes(&queue,&QAbstractItemModel::dataChanged);
        queue.enqueueRows({0,1,2}); queue.activate(1); queue.removeAt(0);
        QCOMPARE(queue.currentRow(),model.rowOfId("b")); QVERIFY(!changes.isEmpty());
        const QString path=QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("playlists.json");
        QVERIFY(QDir().mkpath(path));
        QVERIFY(!queue.saveQueueAsPlaylist("new").startsWith("已")); QVERIFY(queue.playlistNames().isEmpty());
    }
    void playbackLoadsAndPauses() {
        QTemporaryDir temp;
        QByteArray wav("RIFF",4), size(4,0); const int samples=44100;
        qToLittleEndian<quint32>(36+samples*2,size.data()); wav+=size; wav+="WAVEfmt ";
        wav+=QByteArray::fromHex("100000000100010044ac00008858010002001000"); wav+="data";
        qToLittleEndian<quint32>(samples*2,size.data()); wav+=size; wav+=QByteArray(samples*2,0);
        const QString audio=temp.filePath("silence.wav"); writeFile(audio,wav);
        PlayerController player; player.setVolume(0);
        QVERIFY(player.playFile(audio)); QTRY_VERIFY_WITH_TIMEOUT(player.duration()>0,5000);
        player.pause(); QTRY_VERIFY_WITH_TIMEOUT(!player.playing(),2000);
        player.seek(200); QTRY_VERIFY_WITH_TIMEOUT(player.position()>=200,2000);
        player.stop(); QVERIFY(!player.playFile(temp.filePath("missing.wav"))); QVERIFY(!player.errorText().isEmpty());
    }
    void scaleMeasurements() {
        for (int count : {1000,10000,30000}) {
            LibraryManager model; AlbumModel albums(&model); albums.setAutoMergeAlbums(false); QVector<Track> tracks;
            for(int i=0;i<count;++i) {
                auto t=song(QString::number(i),QString("Artist %1").arg(i%5),QString("Album %1").arg(i/10),QString("/missing/%1/%2.mp3").arg(i/10).arg(i),i%10);
                t.albumArtist="Label"; t.coverPath=QString("/missing/%1.png").arg(i); tracks.append(t);
            }
            QElapsedTimer timer; timer.start(); QVERIFY(model.replaceFromJsonObject(library(tracks))); auto load=timer.elapsed();
            timer.restart(); albums.setAutoMergeAlbums(true); auto group=timer.nsecsElapsed()/1000000.0;
            timer.restart(); QVERIFY(model.mergeFromJsonObject(library(tracks))); auto merge=timer.elapsed();
            timer.restart(); for(int i=0;i<count;++i) QVERIFY(model.rowOfId(QString::number(i))>=0); auto ids=timer.nsecsElapsed()/1000000.0;
            QCOMPARE(model.count(),count); QCOMPARE(albums.count(),count/10);
            qInfo().noquote()<<QString("PERF tracks=%1 load_ms=%2 album_regroup_ms=%3 duplicate_merge_ms=%4 all_id_lookups_ms=%5").arg(count).arg(load).arg(group).arg(merge).arg(ids);
        }
    }
    void qmlSmoke() {
        LibraryManager model; AlbumModel albums(&model); PlaybackQueue queue(&model); PlayerController player; LyricModel lyrics; ScriptBridge scripts(&model);
        const QString fixture=qEnvironmentVariable("TNUXMUSIC_TEST_LIBRARY");
        if(!fixture.isEmpty()) QVERIFY(model.replaceFromJsonObject(QJsonDocument::fromJson(readFile(fixture)).object()));
        else {
            QVector<Track> tracks; for(int i=0;i<24;++i) tracks.append(song(QString::number(i),"Artist",QString("Album %1").arg(i/4),QString("/tmp/%1.mp3").arg(i)));
            QVERIFY(model.replaceFromJsonObject(library(tracks)));
        }
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("libraryManager",&model); engine.rootContext()->setContextProperty("albumModel",&albums);
        engine.rootContext()->setContextProperty("queueModel",&queue); engine.rootContext()->setContextProperty("playerController",&player);
        engine.rootContext()->setContextProperty("lyricModel",&lyrics); engine.rootContext()->setContextProperty("scriptBridge",&scripts);
        QStringList errors; connect(&engine,&QQmlEngine::warnings,[&](const QList<QQmlError> &warnings){for(const auto &e:warnings) if(!e.description().contains("Cannot open:")) errors.append(e.toString());});
        engine.load(QUrl::fromLocalFile(QStringLiteral(TNUXMUSIC_QML_FILE)));
        QVERIFY(!engine.rootObjects().isEmpty()); auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        const QString output=qEnvironmentVariable("TNUXMUSIC_TEST_ARTIFACTS");
        auto snapshot=[&](const QString &name) {
            QTest::qWait(name == "albums" ? 2500 : 500);
            if(!output.isEmpty()) { QDir().mkpath(output); auto image=window->grabWindow(); QVERIFY(!image.isNull()); QVERIFY(image.save(QDir(output).filePath(name+".png"))); }
        };
        snapshot("albums");
        window->setProperty("currentTab",0); snapshot("songs");
        model.setSearchQuery("unlikely-search-no-results"); snapshot("empty-search"); model.setSearchQuery("");
        if(albums.rowCount()>0) {
            window->setProperty("currentTab",1);
            const QString key=albums.data(albums.index(0),AlbumModel::KeyRole).toString();
            QVERIFY(QMetaObject::invokeMethod(window,"openAlbum",Q_ARG(QVariant,key))); snapshot("album-detail");
        }
        if(albums.count()>1) {
            const auto choices=albums.choices();
            QObject *dialog=window->findChild<QObject*>("mergeAlbumDialog");
            QObject *target=window->findChild<QObject*>("mergeTarget");
            QVERIFY(dialog); QVERIFY(target);
            const QString source=choices[0].toMap().value("key").toString();
            dialog->setProperty("sourceKey",source); dialog->setProperty("sourceTitle",albums.info(source).value("album"));
            dialog->setProperty("sourceCount",albums.info(source).value("trackCount"));
            target->setProperty("model",albums.choices(source)); target->setProperty("currentIndex",0);
            QVERIFY(QMetaObject::invokeMethod(dialog,"open")); snapshot("merge-preview");
            QVERIFY(QMetaObject::invokeMethod(dialog,"reject"));
        }
        queue.enqueueRows({0,1,2}); window->setProperty("currentTab",2); snapshot("queue");
        window->setWidth(960); window->setHeight(640); snapshot("compact-queue");
        window->setProperty("currentTab",0); window->setProperty("showLyrics",true); snapshot("compact-lyrics");
        if(!errors.isEmpty()) qWarning().noquote()<<errors.join('\n');
        QVERIFY(errors.isEmpty());
    }
};

int main(int argc,char **argv)
{
    QTemporaryDir temp(QDir::tempPath()+"/tnuxmusic-test-XXXXXX");
    qputenv("XDG_DATA_HOME",temp.filePath("data").toUtf8()); qputenv("XDG_CONFIG_HOME",temp.filePath("config").toUtf8());
    QGuiApplication app(argc,argv); app.setOrganizationName("tnux-test"); app.setApplicationName("tnuxmusic-tests");
    RegressionTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "RegressionTests.moc"
