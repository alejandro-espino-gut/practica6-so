// filesystem.h
#ifndef FILESYSTEM_H
#define FILESYSTEM_H
#include "events.h"

// TODO: Diseñar e implementar sobre RTC Slow Memory (RTC_DATA_ATTR)
void fs_init();
void fs_log_event(LogEvent_t evt);
void fs_dump_records();

#endif