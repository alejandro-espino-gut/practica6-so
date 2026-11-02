// heart_sim.cpp
#include "heart_sim.h"
#include "events.h"
#include <Arduino.h>

#define PIN_SW1 32
#define PIN_SW2 33
#define PIN_LED_HEART 25

static QueueHandle_t q_out;

void heart_task(void *pvParameters) {
    while (1) {
        bool sw1 = digitalRead(PIN_SW1);
        bool sw2 = digitalRead(PIN_SW2);
        
        uint32_t delay_ms = 800; // Por defecto: 00 Normal (~75 BPM)
        EventType current_type = EVT_HEART_NORMAL;

        if (!sw1 && sw2) { // 01: Bradicardia
            delay_ms = 1800;
            current_type = EVT_HEART_BRADY;
        } else if (sw1 && !sw2) { // 10: Taquicardia
            delay_ms = 300;
            current_type = EVT_HEART_TACHY;
        } else if (sw1 && sw2) { // 11: Arritmia errática
            delay_ms = random(200, 2000);
            current_type = EVT_HEART_ARRHYTHMIA;
        }

        // Pulso del corazón
        digitalWrite(PIN_LED_HEART, HIGH);
        vTaskDelay(pdMS_TO_TICKS(50));
        digitalWrite(PIN_LED_HEART, LOW);

        // Enviar evento
        LogEvent_t evt = {millis(), current_type, delay_ms};
        xQueueSend(q_out, &evt, 0);

        vTaskDelay(pdMS_TO_TICKS(delay_ms - 50));
    }
}

void HeartSim_Init(QueueHandle_t out_queue) {
    q_out = out_queue;
    pinMode(PIN_SW1, INPUT_PULLUP);
    pinMode(PIN_SW2, INPUT_PULLUP);
    pinMode(PIN_LED_HEART, OUTPUT);
    xTaskCreatePinnedToCore(heart_task, "HeartTask", 2048, NULL, 1, NULL, 0);
}