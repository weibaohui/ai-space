#ifndef APP_TASK_H
#define APP_TASK_H

#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

/* 互斥锁保护进度和完成标记，业务结果在 join 后读取。 */
typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    bool done;
    float percent;
    char message[256];
} app_task_t;

int app_task_start(app_task_t *task, void *(*worker)(void *), void *arg);
void app_task_progress(float percent, const char *message, void *user);
void app_task_complete(app_task_t *task);
bool app_task_poll(app_task_t *task, float *percent, char *message, size_t cap);
/* 等待工作线程退出，并释放任务的互斥锁。 */
void app_task_join(app_task_t *task);

#endif
