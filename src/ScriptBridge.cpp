#include "ScriptBridge.h"

#include "LibraryManager.h"
#include "Track.h"

#include <QFile>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValue>

ScriptBridge::ScriptBridge(LibraryManager *library, QObject *parent)
    : QObject(parent)
    , m_library(library)
{
}

QString ScriptBridge::runScript(const QString &scriptUrl)
{
    if (!m_library)
        return QStringLiteral("脚本失败：曲库未连接");

    const QString path = canonicalLocalPath(scriptUrl);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("脚本失败：无法读取 %1").arg(path);

    QJSEngine engine;
    engine.installExtensions(QJSEngine::ConsoleExtension);

    const QJsonObject before = m_library->toJsonObject();
    const QVariant beforeVariant = QJsonDocument(before).toVariant();
    QJSValue libraryValue = engine.toScriptValue(beforeVariant);
    engine.globalObject().setProperty("library", libraryValue);

    const QString code = QString::fromUtf8(f.readAll());
    QJSValue eval = engine.evaluate(code, path);
    if (eval.isError()) {
        return QStringLiteral("脚本错误：%1:%2 %3")
            .arg(path)
            .arg(eval.property("lineNumber").toInt())
            .arg(eval.toString());
    }

    QJSValue result;
    QJSValue organize = engine.globalObject().property("organize");
    if (organize.isCallable()) {
        result = organize.call({engine.globalObject().property("library")});
        if (result.isError()) {
            return QStringLiteral("organize() 错误：%1:%2 %3")
                .arg(path)
                .arg(result.property("lineNumber").toInt())
                .arg(result.toString());
        }
    }

    if (!result.isObject())
        result = engine.globalObject().property("library");

    const QJsonDocument doc = QJsonDocument::fromVariant(result.toVariant());
    if (!doc.isObject())
        return QStringLiteral("脚本失败：脚本必须返回 library 对象，或修改全局 library");

    QString error;
    if (!m_library->replaceFromJsonObject(doc.object(), &error))
        return QStringLiteral("脚本产物无效：%1").arg(error);

    m_library->save();
    return QStringLiteral("脚本整理完成：%1 首").arg(m_library->count());
}

