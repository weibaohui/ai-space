#include "app_task.h"

#include <stdio.h>

int app_task_start(app_task_t *task, void *(*worker)(void *), void *arg) {
    if (pthread_mutex_init(&task->mutex, NULL) != 0) return -1;
    if (pthread_create(&task->thread, NULL, worker, arg) != 0) {
        pthread_mutex_destroy(&task->mutex);
        return -1;
    }
    return 0;
}

void app_task_progress(float percent, const char *message, void *user) {
    app_task_t *task = user;
    pthread_mutex_lock(&task->mutex);
    task->percent = percent;
    if (message) snprintf(task->message, sizeof(task->message), "%s", message);
    pthread_mutex_unlock(&task->mutex);
}

void app_task_complete(app_task_t *task) {
    pthread_mutex_lock(&task->mutex);
    task->done = true;
    pthread_mutex_unlock(&task->mutex);
}

bool app_task_poll(app_task_t *task, float *percent, char *message, size_t cap) {
    pthread_mutex_lock(&task->mutex);
    bool done = task->done;
    *percent = task->percent;
    if (task->message[0]) snprintf(message, cap, "%s", task->message);
    pthread_mutex_unlock(&task->mutex);
    return done;
}

void app_task_join(app_task_t *task) {
    pthread_join(task->thread, NULL);
    pthread_mutex_destroy(&task->mutex);
}
