#pragma once
#include <QObject>

class SoundUtil : public QObject {
    Q_OBJECT
public:
    enum class Sound { Thinking, Complete, Question, Error };
    static void play(Sound type);
};
