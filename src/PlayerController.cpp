#include "PlayerController.h"

#include "Track.h"

#include <QFileInfo>
#include <QUrl>
#include <algorithm>
#include <cmath>

PlayerController::PlayerController(QObject *parent)
    : QObject(parent)
{
    m_audio.setVolume(0.75);
    m_player.setAudioOutput(&m_audio);

    connect(&m_player, &QMediaPlayer::positionChanged, this, &PlayerController::positionChanged);
    connect(&m_player, &QMediaPlayer::durationChanged, this, &PlayerController::durationChanged);
    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, &PlayerController::playingChanged);
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &errorString) {
        setErrorText(errorString);
    });
}

void PlayerController::playFile(const QString &pathOrUrl)
{
    const QString path = canonicalLocalPath(pathOrUrl);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        setErrorText(QStringLiteral("音频不存在：%1").arg(path));
        return;
    }

    setErrorText({});
    m_source = path;
    emit sourceChanged();
    m_player.setSource(QUrl::fromLocalFile(path));
    m_player.play();
}

void PlayerController::toggle()
{
    if (m_player.playbackState() == QMediaPlayer::PlayingState)
        m_player.pause();
    else
        m_player.play();
}

void PlayerController::pause()
{
    m_player.pause();
}

void PlayerController::stop()
{
    m_player.stop();
}

void PlayerController::seek(qint64 ms)
{
    m_player.setPosition(ms);
}

void PlayerController::setVolume(double volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    if (std::abs(static_cast<double>(m_audio.volume()) - volume) < 0.0001)
        return;
    m_audio.setVolume(volume);
    emit volumeChanged();
}

void PlayerController::setErrorText(const QString &text)
{
    if (m_errorText == text)
        return;
    m_errorText = text;
    emit errorTextChanged();
}
