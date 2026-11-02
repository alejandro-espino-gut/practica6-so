// pacemaker.cpp
#include "pacemaker.h"
#include "events.h"
#include <Arduino.h>

#define PIN_LED_PACE 26
#define BRADY_THRESHOLD_MS 1200 

static QueueHandle_t q_in;
static QueueHandle_t q_fs;

void pacemaker_task(void *pvParameters) {
    LogEvent_t incoming_evt;
    while (1) {
        if (xQueueReceive(q_in, &incoming_evt, portMAX_DELAY)) {
            // Reenviar latido al File System para registro histórico
            xQueueSend(q_fs, &incoming_evt, 0);

            // Lógica del marcapasos (reaccionar a bradicardia o latido faltante)
            if (incoming_evt.interval_ms > BRADY_THRESHOLD_MS || incoming_evt.type == EVT_HEART_BRADY) {
                // Simular disparo del marcapasos
                digitalWrite(PIN_LED_PACE, HIGH);
                vTaskDelay(pdMS_TO_TICKS(30));
                digitalWrite(PIN_LED_PACE, LOW);

                // Registrar la intervención médica en el File System
                LogEvent_t pace_evt = {millis(), EVT_PACE_FIRED, 0};
                xQueueSend(q_fs, &pace_evt, 0);
            }
        }
    }
}

void Pacemaker_Init(QueueHandle_t in_queue, QueueHandle_t fs_queue) {
    q_in = in_queue;
    q_fs = fs_queue;
    pinMode(PIN_LED_PACE, OUTPUT);
    xTaskCreatePinnedToCore(pacemaker_task, "PaceTask", 2048, NULL, 2, NULL, 1);
}