#include "SoundUtil.h"
#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QUrl>
#include <QDebug>

static QString soundPath(const char *name) {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/sounds/") + name;
}

void SoundUtil::play(Sound type) {
    QString path;
    switch (type) {
    case Sound::Thinking: path = soundPath("question.mp3"); break;
    case Sound::Complete: path = soundPath("complete.mp3"); break;
    case Sound::Question: path = soundPath("question.mp3"); break;
    case Sound::Error:    path = soundPath("error.mp3");    break;
    }
    if (path.isEmpty() || !QFile::exists(path)) {
        qWarning() << "SoundUtil: archivo de sonido no encontrado:" << path;
        QApplication::beep();
        return;
    }

    static QAudioOutput *audio = [] {
        auto *output = new QAudioOutput(qApp);
        output->setVolume(0.85f);
        return output;
    }();
    static QMediaPlayer *player = [] {
        auto *mediaPlayer = new QMediaPlayer(qApp);
        mediaPlayer->setAudioOutput(audio);
        QObject::connect(mediaPlayer, &QMediaPlayer::errorOccurred, mediaPlayer,
                         [](QMediaPlayer::Error, const QString &message) {
            qWarning() << "SoundUtil:" << message;
            QApplication::beep();
        });
        return mediaPlayer;
    }();

    player->stop();
    player->setSource(QUrl::fromLocalFile(path));
    player->setPosition(0);
    player->play();
}
