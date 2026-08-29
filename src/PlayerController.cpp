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
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            m_wantsPlayback = false;
            queueFinished();
        } else if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
            // Some backends need the play request repeated once loading has
            // completed.  m_wantsPlayback deliberately remains true while the
            // track is playing, so a stale state signal cannot eat a new request.
            tryStartPlayback();
        }
    });
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &errorString) {
        m_wantsPlayback = false;
        const QString message = errorString.isEmpty() ? QStringLiteral("无法播放音频") : errorString;
        setErrorText(message);
        queueFailure(message);
    });
}

bool PlayerController::playFile(const QString &pathOrUrl)
{
    const QString path = canonicalLocalPath(pathOrUrl);
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        ++m_commandGeneration;
        m_wantsPlayback = false;
        setErrorText(QStringLiteral("音频不存在：%1").arg(pathOrUrl));
        return false;
    }

    ++m_commandGeneration;
    setErrorText({});
    m_wantsPlayback = true;
    m_source = path;
    emit sourceChanged();

    const QUrl source = QUrl::fromLocalFile(path);
    if (m_player.source() == source)
        m_player.setPosition(0);
    else
        m_player.setSource(source);

    // QMediaPlayer::play() is valid while a source is loading.  Calling it now
    // records the request in the backend; tryStartPlayback() repeats it after
    // LoadedMedia/BufferedMedia for backends that lose the first request.
    tryStartPlayback();
    return true;
}

void PlayerController::toggle()
{
    ++m_commandGeneration;
    if (m_player.playbackState() == QMediaPlayer::PlayingState) {
        m_wantsPlayback = false;
        m_player.pause();
    } else {
        m_wantsPlayback = true;
        tryStartPlayback();
    }
}

void PlayerController::pause()
{
    ++m_commandGeneration;
    m_wantsPlayback = false;
    m_player.pause();
}

void PlayerController::stop()
{
    ++m_commandGeneration;
    m_wantsPlayback = false;
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

void PlayerController::tryStartPlayback()
{
    if (!m_wantsPlayback || m_player.source().isEmpty())
        return;

    const auto status = m_player.mediaStatus();
    if (status == QMediaPlayer::NoMedia || status == QMediaPlayer::InvalidMedia) {
        return;
    }

    m_player.play();
}

void PlayerController::queueFinished()
{
    const quint64 generation = ++m_commandGeneration;
    QMetaObject::invokeMethod(this, [this, generation] {
        if (generation != m_commandGeneration || m_player.mediaStatus() != QMediaPlayer::EndOfMedia)
            return;
        emit finished();
    }, Qt::QueuedConnection);
}

void PlayerController::queueFailure(const QString &message)
{
    const quint64 generation = ++m_commandGeneration;
    QMetaObject::invokeMethod(this, [this, generation, message] {
        if (generation != m_commandGeneration)
            return;
        emit failed(message);
    }, Qt::QueuedConnection);
}
