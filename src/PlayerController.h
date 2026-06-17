#pragma once

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QObject>

class PlayerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString source READ source NOTIFY sourceChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

public:
    explicit PlayerController(QObject *parent = nullptr);

    qint64 position() const { return m_player.position(); }
    qint64 duration() const { return m_player.duration(); }
    bool playing() const { return m_player.playbackState() == QMediaPlayer::PlayingState; }
    double volume() const { return m_audio.volume(); }
    QString source() const { return m_source; }
    QString errorText() const { return m_errorText; }

    Q_INVOKABLE void playFile(const QString &pathOrUrl);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 ms);
    void setVolume(double volume);

signals:
    void positionChanged();
    void durationChanged();
    void playingChanged();
    void volumeChanged();
    void sourceChanged();
    void errorTextChanged();
    void finished();

private:
    QMediaPlayer m_player;
    QAudioOutput m_audio;
    QString m_source;
    QString m_errorText;

    void setErrorText(const QString &text);
};
