#pragma once

#include <QObject>

class LibraryManager;

class ScriptBridge : public QObject {
    Q_OBJECT

public:
    explicit ScriptBridge(LibraryManager *library, QObject *parent = nullptr);

    Q_INVOKABLE QString runScript(const QString &scriptUrl);

private:
    LibraryManager *m_library = nullptr;
};

