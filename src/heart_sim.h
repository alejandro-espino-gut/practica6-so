// heart_sim.h
#ifndef HEART_SIM_H
#define HEART_SIM_H
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void HeartSim_Init(QueueHandle_t out_queue);

#endif