#pragma once
#include <QString>

enum class TaskStatus { Pending, Running, Done, Error };
enum class QueueStatus { Idle, Working, Paused, Done };

struct Task {
    QString id;
    QString title;
    TaskStatus status;
};
