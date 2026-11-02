#include <Arduino.h>
#include "events.h"
#include "heart_sim.h"
#include "pacemaker.h"
#include "filesystem.h" // Modulo a cargo del alumno

QueueHandle_t heart_queue;
QueueHandle_t fs_queue;

#define PIN_RESET 14

// Esta función se ejecuta inmediatamente al presionar el botón
void IRAM_ATTR trigger_reset() {
    ESP.restart(); // Fuerza un reinicio inmediato (simula un corte/watchdog)
}

void fs_writer_task(void *pvParameters) {
    LogEvent_t evt_to_write;
    while(1) {
        if (xQueueReceive(fs_queue, &evt_to_write, portMAX_DELAY)) {
            // Delega la escritura atómica a la librería del alumno
            fs_log_event(evt_to_write);
        }
    }
}

void setup() {
    Serial.begin(115200);

    // Configurar el botón de reset simulado
    pinMode(PIN_RESET, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_RESET), trigger_reset, FALLING);
    
    // Inicializar sistema de archivos (recuperación de fallos)
    fs_init();

    // Creación de colas seguras
    heart_queue = xQueueCreate(10, sizeof(LogEvent_t));
    fs_queue = xQueueCreate(20, sizeof(LogEvent_t));

    // Lanzar módulos
    HeartSim_Init(heart_queue);
    Pacemaker_Init(heart_queue, fs_queue);

    // Tarea de muy baja prioridad para no interrumpir misión crítica
    xTaskCreatePinnedToCore(fs_writer_task, "FS_Writer", 2048, NULL, 0, NULL, 1);
}

void loop() {
    // Monitoreo serial para pruebas (comandos del médico/auditor)
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'd') {
            Serial.println(">>> Volcando registros históricos...");
            fs_dump_records();
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
}