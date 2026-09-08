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

    if (m_library->busy()) return QStringLiteral("曲库任务进行中，请稍候");
    const QString path = canonicalLocalPath(scriptUrl);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("脚本失败：无法读取 %1").arg(path);

    QJSEngine engine;
    engine.installExtensions(QJSEngine::ConsoleExtension);

    const QJsonObject before = m_library->toJsonObject();
    // Qt 6.8 exposes nested QVariant collections as wrappers whose mutations
    // may not survive conversion back. Scripts must receive ordinary JS data.
    const QJSValue json = engine.globalObject().property("JSON");
    const QJSValue parse = json.property("parse");
    const QJSValue stringify = json.property("stringify");
    QJSValue libraryValue = parse.call({QString::fromUtf8(QJsonDocument(before).toJson(QJsonDocument::Compact))});
    if (libraryValue.isError())
        return QStringLiteral("脚本失败：无法准备曲库对象");
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

    const QJSValue serialized = stringify.call({result});
    if (serialized.isError() || !serialized.isString())
        return QStringLiteral("脚本失败：产物必须可序列化为 JSON（不能包含循环引用）");
    const QJsonDocument doc = QJsonDocument::fromJson(serialized.toString().toUtf8());
    if (!doc.isObject())
        return QStringLiteral("脚本失败：脚本必须返回 library 对象，或修改全局 library");

    QString error;
    if (!m_library->replaceAndSave(doc.object(), &error))
        return QStringLiteral("脚本产物无效：%1").arg(error);

    return QStringLiteral("脚本整理完成：%1 首").arg(m_library->count());
}

