#pragma once

#include <stdint.h>

// Modelo de dominio independiente de LVGL y del origen futuro de datos.
enum class MovementType : uint8_t {
    Credit,
    Debit
};

enum class MovementOrigin : uint8_t {
    BancoEscolar,
    PanelMaestro
};

struct Student {
    uint16_t student_id;
    const char *nfc_uid;
    const char *name;
    const char *preferred_name;
    uint8_t grade;
    const char *group;
    int16_t roster_number;
    const char *avatar_asset;
    const char *level; // TEMPORAL: pendiente de definición; no deriva del saldo.
};

struct ReadingRecord {
    uint16_t student_id;
    const char *date;
    uint16_t ppm;
    bool applied;
};

struct WritingRecord {
    uint16_t student_id;
    const char *date;
    uint16_t total_words;
    int16_t errors;
    bool applied;
};

struct AccountRecord {
    uint16_t student_id;
    int32_t aureos;
};

struct StudentMovement {
    uint16_t student_id;
    int32_t amount;
    MovementType type;
    const char *reason;
    const char *date_time;
    MovementOrigin origin;
};

struct AttendanceRecord {
    uint16_t student_id;
    const char *date;
    const char *time;
    const char *status;
};

// Punto único para cambiar el saldo cuando llegue el servicio de datos.
void setStudentBalance(int32_t aureos);
int32_t getStudentBalance();
void updateBalanceUI();
