// pacemaker.h
#ifndef PACEMAKER_H
#define PACEMAKER_H
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void Pacemaker_Init(QueueHandle_t in_queue, QueueHandle_t fs_queue);

#endif