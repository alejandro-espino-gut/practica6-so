#include "filesystem.h"
#include <Arduino.h>
#ifndef FS_N_SLOTS
#define FS_N_SLOTS 1749          
#endif

#define FS_MAGIC 0xA5C3u
#define FS_VERSION 1u
#define FS_TS_MASK 0x00FFFFFFu

typedef struct {
    uint32_t header;
    uint32_t slots[FS_N_SLOTS];
} fs_rtc_t;

static_assert(sizeof(fs_rtc_t) <= 7168, "El FS excede 7 KB, deja >= 1 KB libre en la RTC");

static RTC_NOINIT_ATTR fs_rtc_t fs_rtc;

static volatile uint32_t *const S = fs_rtc.slots;

static uint16_t cnt[4];     
static uint16_t reserve_[4];
static uint32_t evicteado[4]; 
static uint16_t free_cnt;   
static uint16_t free_hint;  
static uint32_t ts_base;    

static const char *TYPE_NAME[4] = {"Sinusal", "Bradicardia", "Taquicardia", "Arritmia"};

// Funciones Auxiliares
static uint8_t crc8(const uint8_t *d, size_t n) {
    uint8_t c = 0;
    while (n--) {
        c ^= *d++;
        for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    }
    return c;
}

static uint32_t setear_cabecera() {
    uint32_t lo = FS_MAGIC | (FS_VERSION << 16);
    uint8_t b[3] = { (uint8_t)lo, (uint8_t)(lo >> 8), (uint8_t)(lo >> 16) };
    return lo | ((uint32_t)crc8(b, 3) << 24);
}

static bool cabecera_ok(uint32_t h) {
    uint8_t b[3] = { (uint8_t)h, (uint8_t)(h >> 8), (uint8_t)(h >> 16) };
    return (h & 0xFFFF) == FS_MAGIC && ((h >> 16) & 0xFF) == FS_VERSION && (h >> 24) == crc8(b, 3);
}

static inline uint32_t codificar_slot(uint8_t type, uint32_t ts24) {
    uint32_t w = 1u | ((uint32_t)(type & 3) << 1) | ((ts24 & FS_TS_MASK) << 3);
    if (__builtin_popcount(w) & 1) w |= (1u << 27);   // paridad par
    return w;
}
static inline bool slot_valido(uint32_t w) { 
  return (w & 1) && !(w >> 28) && !(__builtin_popcount(w) & 1); 
}

static inline uint8_t tipo_slot(uint32_t w)  { 
  return (w >> 1) & 3; 
}

static inline uint32_t slot_ts(uint32_t w) { 
  return (w >> 3) & FS_TS_MASK; 
}

// diferencia a - b con aritmetica modular de 24 bits (para desbordamiento)
static inline int32_t ts_diff(uint32_t a, uint32_t b) {
    int32_t d = (int32_t)((a - b) & FS_TS_MASK);
    if (d & 0x800000) d -= 0x1000000;
    return d;
}

static void calcular_reservas() {
    // arritmia 37.5 %, taquicardia 25 %, bradicardia 25 %, sinusal el resto
    reserve_[3] = (uint16_t)(FS_N_SLOTS * 3 / 8);
    reserve_[2] = (uint16_t)(FS_N_SLOTS / 4);
    reserve_[1] = (uint16_t)(FS_N_SLOTS / 4);
    reserve_[0] = (uint16_t)(FS_N_SLOTS - reserve_[3] - reserve_[2] - reserve_[1]);
}

static const char *motivo_reset() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "encendido (power-on)";
        case ESP_RST_EXT: return "pin EN externo";
        case ESP_RST_SW: return "software (esp_restart)";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "watchdog de interrupciones";
        case ESP_RST_TASK_WDT: return "watchdog de tareas";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_DEEPSLEEP:return "deep sleep";
        default: return "desconocido";
    }
}

// inicializacion/recuperacion
void fs_init() {
    Serial.println("[FS] Inicializando sistema de archivos (Base vacia)...");
    calcular_reservas();
    memset(cnt, 0, sizeof(cnt));
    memset(evicteado, 0, sizeof(evicteado));
    free_cnt = 0; free_hint = 0; ts_base = 0;

    Serial.printf("[FS] Motivo de reinicio: %s\n", motivo_reset());

    // validar cabecera. si es invalida, la RTC es basura o fue borrada => formatear
    if (!cabecera_ok(fs_rtc.header)) {
        Serial.println("[FS] Cabecera invalida (primer arranque o RTC borrada): formateando...");
        for (int i = 0; i < FS_N_SLOTS; i++) S[i] = 0;
        __sync_synchronize();
        fs_rtc.header = setear_cabecera(); // la cabecera se escribe al final del formateo
        free_cnt = FS_N_SLOTS;
        Serial.printf("[FS] Formateado. validos=0, capacidad=%d slots\n", FS_N_SLOTS);
        return;
    }

    // checar slots: valido / vacio / descartado
    uint32_t descartado = 0, valid = 0, max_ts = 0;
    for (int i = 0; i < FS_N_SLOTS; i++) {
        uint32_t w = S[i];
        if (w == 0) { free_cnt++; continue; }
        if (slot_valido(w)) {
            cnt[tipo_slot(w)]++;
            uint32_t t = slot_ts(w);
            if (valid == 0 || ts_diff(t, max_ts) > 0) max_ts = t;
            valid++;
        } else {
            S[i] = 0; // truncado o corrupto, se descarta
            free_cnt++;
            descartado++;
        }
    }
    ts_base = valid ? ((max_ts + 1) & FS_TS_MASK) : 0;

    Serial.printf("[FS] Recuperacion: validos=%u, descartados=%u, libres=%u\n",
                  (unsigned)valid, (unsigned)descartado, (unsigned)free_cnt);
    Serial.printf("[FS] Sinusal=%u Bradicardia=%u Taquicardia=%u Arritmia=%u\n",
                  cnt[0], cnt[1], cnt[2], cnt[3]);
}

// escritura
static int buscar_libre() {
    for (int i = 0; i < FS_N_SLOTS; i++) {
        int idx = (free_hint + i) % FS_N_SLOTS;
        if (S[idx] == 0) { free_hint = (uint16_t)((idx + 1) % FS_N_SLOTS); return idx; }
    }
    return -1;
}

// slot del registro mas viejo de la clase k (-1 si no hay)
static int mas_viejo(uint8_t k) {
    int mejor = -1; uint32_t bts = 0;
    for (int i = 0; i < FS_N_SLOTS; i++) {
        uint32_t w = S[i];
        if (slot_valido(w) && tipo_slot(w) == k) {
            uint32_t t = slot_ts(w);
            if (mejor < 0 || ts_diff(t, bts) < 0) { mejor = i; bts = t; }
        }
    }
    return mejor;
}

// solo la tarea Task_Writer llama a esta funcion, como es el único escritor evitamos condiciones de carrera.
void fs_log_event(LogEvent_t evt) {
    if (evt.type > EVT_HEART_ARRHYTHMIA) return; 

    // 0..3 coincide con el codigo del slot
    uint8_t  c  = (uint8_t)evt.type; 
    uint32_t ts = (ts_base + evt.timestamp / 1000u) & FS_TS_MASK;
    uint32_t w  = codificar_slot(c, ts);

    int objetivo = -1;
    if (free_cnt > 0) {
        objetivo = buscar_libre();
        if (objetivo >= 0) free_cnt--;
    }
    if (objetivo < 0) {
        // memoria llena, elegir victima segun prioridad clinica arritmia > taqui > bradi > sinusal
        int vk = -1;
        if (cnt[c] < reserve_[c]) {
            // la entrante esta dentro de su reserva, toma espacio de la clase de menor prioridad que exceda la suya
            for (int k = 0; k < 4; k++) if (k != c && cnt[k] > reserve_[k]) { vk = k; break; }
        } else {
            // solo puede desalojar a clases de menor prioridad que excedan su reserva
            for (int k = 0; k < c; k++) if (cnt[k] > reserve_[k]) { vk = k; break; }
            if (vk < 0) vk = c;                      
        }
        if (vk < 0) vk = c;
        objetivo = mas_viejo((uint8_t)vk);
        if (objetivo < 0) {                            
            for (int k = 0; k < 4 && objetivo < 0; k++) { objetivo = mas_viejo((uint8_t)k); vk = k; }
            if (objetivo < 0) return;
        }
        cnt[vk]--;
        evicteado[vk]++;
    }

    // commit es un solo store de 32 bits (reemplaza al registro viejo de una)
    S[objetivo] = w;      
    cnt[c]++;
}

// lectura / volcado
static uint32_t snap[FS_N_SLOTS];
static uint16_t ord[FS_N_SLOTS];

static int cmp_ts(const void *a, const void *b) {
    uint16_t ia = *(const uint16_t *)a, ib = *(const uint16_t *)b;
    int32_t d = ts_diff(slot_ts(snap[ia]), slot_ts(snap[ib]));
    if (d) return d < 0 ? -1 : 1;
    return (int)ia - (int)ib;
}

void fs_dump_records() {
    uint32_t n = 0, c[4] = {0, 0, 0, 0};
    for (int i = 0; i < FS_N_SLOTS; i++) {           
        uint32_t w = S[i];
        snap[i] = w;
        if (slot_valido(w)) { ord[n++] = (uint16_t)i; c[tipo_slot(w)]++; }
    }
    qsort(ord, n, sizeof(uint16_t), cmp_ts);

    Serial.println("[FS] Volcado de registros (Base vacia)...");
    Serial.printf("[FS] validos=%u de %d | Sinusal=%u Bradi=%u Taqui=%u Arritmia=%u\n",
                  (unsigned)n, FS_N_SLOTS, (unsigned)c[0], (unsigned)c[1], (unsigned)c[2], (unsigned)c[3]);
    Serial.printf("[FS] desalojados (desde arranque): Sinusal=%u Bradi=%u Taqui=%u Arritmia=%u\n",
                  (unsigned)evicteado[0], (unsigned)evicteado[1], (unsigned)evicteado[2], (unsigned)evicteado[3]);
    Serial.printf("[FS] reservas: %u / %u / %u / %u\n", reserve_[0], reserve_[1], reserve_[2], reserve_[3]);

    for (uint32_t k = 0; k < n; k++) {
        uint32_t w = snap[ord[k]];
        Serial.printf("[FS] #%u slot=%u t=%us %s\n", (unsigned)k, (unsigned)ord[k],
                      (unsigned)slot_ts(w), TYPE_NAME[tipo_slot(w)]);
        if ((k & 63) == 63) vTaskDelay(pdMS_TO_TICKS(1));
    }
    Serial.println("[FS] ===== FIN =====");
}