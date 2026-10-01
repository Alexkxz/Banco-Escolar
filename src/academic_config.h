#pragma once

#include <stddef.h>
#include <stdint.h>

enum class ReadingLevel : uint8_t {
    RequiresSupport,
    NearStandard,
    Standard,
    Advanced
};

struct ReadingFluencyThresholds {
    uint8_t grade;
    uint16_t requires_support_below;
    uint16_t near_standard_from;
    uint16_t standard_from;
    uint16_t advanced_from;
};

struct ReadingFluencyEvaluation {
    ReadingLevel level;
    uint16_t level_lower_bound;
    uint16_t next_level_ppm;
    uint16_t ppm_to_next_level;
    bool has_next_level;
};

// Configuración general académica. No pertenece a ningún alumno concreto.
inline constexpr ReadingFluencyThresholds READING_FLUENCY_THRESHOLDS[] = {
    {1, 15, 15, 35, 60},
    {2, 35, 35, 60, 85},
    {3, 60, 60, 85, 100},
    {4, 85, 85, 100, 115},
    {5, 100, 100, 115, 125},
    {6, 115, 115, 125, 135}
};

inline constexpr size_t READING_FLUENCY_THRESHOLD_COUNT =
    sizeof(READING_FLUENCY_THRESHOLDS) / sizeof(READING_FLUENCY_THRESHOLDS[0]);

inline const ReadingFluencyThresholds *getReadingFluencyThresholds(uint8_t grade)
{
    if (grade < 1 || grade > READING_FLUENCY_THRESHOLD_COUNT) return nullptr;
    return &READING_FLUENCY_THRESHOLDS[grade - 1];
}

inline ReadingFluencyEvaluation evaluateReadingFluency(uint8_t grade, uint16_t ppm)
{
    const ReadingFluencyThresholds *thresholds = getReadingFluencyThresholds(grade);
    if (thresholds == nullptr) return {ReadingLevel::RequiresSupport, 0, 0, 0, false};

    if (ppm < thresholds->near_standard_from) {
        return {ReadingLevel::RequiresSupport, 0, thresholds->near_standard_from,
                static_cast<uint16_t>(thresholds->near_standard_from - ppm), true};
    }
    if (ppm < thresholds->standard_from) {
        return {ReadingLevel::NearStandard, thresholds->near_standard_from,
                thresholds->standard_from, static_cast<uint16_t>(thresholds->standard_from - ppm), true};
    }
    if (ppm < thresholds->advanced_from) {
        return {ReadingLevel::Standard, thresholds->standard_from,
                thresholds->advanced_from, static_cast<uint16_t>(thresholds->advanced_from - ppm), true};
    }
    return {ReadingLevel::Advanced, thresholds->advanced_from, 0, 0, false};
}

inline const char *readingLevelLabel(ReadingLevel level)
{
    switch (level) {
        case ReadingLevel::RequiresSupport: return "Requiere apoyo";
        case ReadingLevel::NearStandard: return "Cerca del est\xC3\xA1ndar";
        case ReadingLevel::Standard: return "Est\xC3\xA1ndar";
        case ReadingLevel::Advanced: return "Avanzado";
    }
    return "Sin evaluar";
}

inline const char *nextReadingLevelLabel(ReadingLevel level)
{
    switch (level) {
        case ReadingLevel::RequiresSupport: return "Cerca del est\xC3\xA1ndar";
        case ReadingLevel::NearStandard: return "Est\xC3\xA1ndar";
        case ReadingLevel::Standard: return "Avanzado";
        case ReadingLevel::Advanced: return nullptr;
    }
    return nullptr;
}
