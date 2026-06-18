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

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("tnux");
    QCoreApplication::setApplicationName("tnuxmusic");
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/tnuxmusic-256.png")));

    LibraryManager library;
    AlbumModel albums(&library);
    PlaybackQueue queue(&library);
    PlayerController player;
    LyricModel lyrics;
    ScriptBridge scripts(&library);

    QObject::connect(&player, &PlayerController::positionChanged, [&] {
        lyrics.setPosition(player.position());
    });

    library.loadDefault();

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

    return app.exec();
}
