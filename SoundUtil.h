#pragma once
#include <QObject>

class SoundUtil : public QObject {
    Q_OBJECT
public:
    enum class Sound { Complete, Question, Error };
    static void play(Sound type);
};
