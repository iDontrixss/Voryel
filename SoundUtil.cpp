#include "SoundUtil.h"
#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <string>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <mmsystem.h>
#endif

static QString soundPath(const char *name) {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/sounds/") + name;
}

void SoundUtil::play(Sound type) {
#if defined(Q_OS_WIN)
    QString path;
    switch (type) {
    case Sound::Complete: path = soundPath("complete.mp3"); break;
    case Sound::Question: path = soundPath("question.mp3"); break;
    case Sound::Error:    path = soundPath("error.mp3");    break;
    }
    if (path.isEmpty() || !QFile::exists(path)) return;
    mciSendString(L"close all", nullptr, 0, nullptr);
    std::wstring cmd = std::wstring(L"open \"") + path.toStdWString() + L"\" type mpegvideo alias sound";
    mciSendString(cmd.c_str(), nullptr, 0, nullptr);
    mciSendString(L"play sound", nullptr, 0, nullptr);
#else
    Q_UNUSED(type);
#endif
}
