#include "AlbumModel.h"
#include "LibraryManager.h"
#include "LyricModel.h"
#include "PlaybackQueue.h"
#include "PlayerController.h"
#include "ScriptBridge.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QDir>
#include <QCryptographicHash>
#include <QQuickWindow>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QTextStream>

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--merge-library" || QByteArray(argv[i]).startsWith("--merge-library=")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("tnux");
    QCoreApplication::setApplicationName("tnuxmusic");
    QCoreApplication::setApplicationVersion(QStringLiteral(TNUXMUSIC_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/tnuxmusic-256.png")));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"merge-library", "Merge a JSON/ZIP library without opening a window.", "source"});
    parser.addOption({"library", "Destination library for command-line merge.", "destination"});
    parser.process(app);
    const bool cliMerge = parser.isSet("merge-library");
    if (parser.isSet("library") && !cliMerge) parser.showHelp(2);
    const QString destination = parser.isSet("library") ? QFileInfo(parser.value("library")).absoluteFilePath() : QString();
    const QString dataRoot = destination.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : QFileInfo(destination).absolutePath();
    QDir().mkpath(dataRoot);
    QLockFile instanceLock(QDir(dataRoot).filePath("instance.lock"));
    instanceLock.setStaleLockTime(0);
    const QString serverName = "tnuxmusic-" + QString::fromLatin1(QCryptographicHash::hash(dataRoot.toUtf8(), QCryptographicHash::Sha256).toHex().left(20));
    if (!instanceLock.tryLock()) {
        if (cliMerge) { qWarning("Library is in use. Close tnuxmusic before merging from the command line."); return 2; }
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(1500)) {
            socket.write("activate"); socket.waitForBytesWritten(1000);
            return 0;
        }
        qWarning("tnuxmusic is already running or its data directory is locked.");
        return 1;
    }
    QLocalServer::removeServer(serverName);
    QLocalServer activationServer;
    activationServer.setSocketOptions(QLocalServer::UserAccessOption);
    activationServer.listen(serverName);

    LibraryManager library(nullptr, destination);
    if (cliMerge) {
        if (QFileInfo::exists(library.libraryPath())) {
            library.loadDefault();
            if (!library.operationSucceeded()) { QTextStream(stderr) << library.lastMessage() << Qt::endl; return 1; }
        }
        QTextStream(stdout) << library.mergeLibrary(parser.value("merge-library")) << Qt::endl;
        return library.operationSucceeded() ? 0 : 1;
    }
    AlbumModel albums(&library);
    PlaybackQueue queue(&library);
    PlayerController player;
    LyricModel lyrics;
    ScriptBridge scripts(&library);

    QObject::connect(&player, &PlayerController::positionChanged, [&] {
        lyrics.setPosition(player.position());
    });



    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("libraryManager", &library);
    engine.rootContext()->setContextProperty("albumModel", &albums);
    engine.rootContext()->setContextProperty("queueModel", &queue);
    engine.rootContext()->setContextProperty("playerController", &player);
    engine.rootContext()->setContextProperty("lyricModel", &lyrics);
    engine.rootContext()->setContextProperty("scriptBridge", &scripts);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("TnuxMusic", "Main");

    QObject::connect(&activationServer, &QLocalServer::newConnection, &app, [&] {
        while (auto *socket = activationServer.nextPendingConnection()) {
            socket->disconnectFromServer(); socket->deleteLater();
            for (auto *object : engine.rootObjects()) if (auto *window = qobject_cast<QQuickWindow *>(object)) {
                if (window->visibility() == QWindow::Minimized) window->showNormal();
                window->raise(); window->requestActivate();
            }
        }
    });
    library.startOperation(QStringLiteral("load"));
    return app.exec();
}
