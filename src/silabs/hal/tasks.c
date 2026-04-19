#include "hal/tasks.h"

#if defined(_SILICON_LABS_32B_SERIES_1)

static void _af_event_handler(sl_zigbee_event_t *event) {
    hal_task_t *task = (hal_task_t *)event->data;

    task->handler(task->arg);
}

void hal_tasks_init(hal_task_t *task) {
    sl_zigbee_event_init(&task->platform_struct, _af_event_handler);
    task->platform_struct.data = (uint32_t)task;
}

void hal_tasks_schedule(hal_task_t *task, uint32_t delay_ms) {
    sl_zigbee_event_set_delay_ms(&task->platform_struct, delay_ms);
}

void hal_tasks_unschedule(hal_task_t *task) {
    sl_zigbee_event_set_inactive(&task->platform_struct);
}

#else

#include "zigbee_app_framework_event.h"
#include <stddef.h>

// Get container structure from embedded member pointer
#define container_of(ptr, type, member) \
        ((type *)((char *)(ptr) - offsetof(type, member)))

static void _af_event_handler(sl_zigbee_af_event_t *event) {
    // Get hal_task_t from embedded platform_struct event
    hal_task_t *task = container_of(event, hal_task_t, platform_struct);

    task->handler(task->arg);
}

void hal_tasks_init(hal_task_t *task) {
    sl_zigbee_af_event_init(&task->platform_struct, _af_event_handler);
}

void hal_tasks_schedule(hal_task_t *task, uint32_t delay_ms) {
    sl_zigbee_af_event_set_delay_ms(&task->platform_struct, delay_ms);
}

void hal_tasks_unschedule(hal_task_t *task) {
    sl_zigbee_af_event_set_inactive(&task->platform_struct);
}

#endif
