#ifndef EVENTS_H
#define EVENTS_H
#include <stdint.h>

typedef enum {
    EVT_HEART_NORMAL = 0,
    EVT_HEART_BRADY,
    EVT_HEART_TACHY,
    EVT_HEART_ARRHYTHMIA,
    EVT_PACE_FIRED
} EventType;

typedef struct {
    uint32_t timestamp;
    EventType type;
    uint32_t interval_ms;
} LogEvent_t;

#endif