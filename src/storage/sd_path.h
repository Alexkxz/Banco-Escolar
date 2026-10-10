#pragma once

#include <stddef.h>

// All public SD paths are relative to /sdcard. Keeping this validator free of
// Arduino dependencies also lets the firmware compile-time test its policy.
enum class SdPathError : unsigned char {
    NONE,
    EMPTY,
    ABSOLUTE,
    TOO_LONG,
    INVALID_CHARACTER,
    INVALID_COMPONENT,
    RESERVED_SUFFIX
};

constexpr bool sd_path_reserved_suffix(const char *path, size_t length)
{
    if (length < 4) return false;
    const char a = path[length - 4] >= 'A' && path[length - 4] <= 'Z'
        ? static_cast<char>(path[length - 4] - 'A' + 'a') : path[length - 4];
    const char b = path[length - 3] >= 'A' && path[length - 3] <= 'Z'
        ? static_cast<char>(path[length - 3] - 'A' + 'a') : path[length - 3];
    const char c = path[length - 2] >= 'A' && path[length - 2] <= 'Z'
        ? static_cast<char>(path[length - 2] - 'A' + 'a') : path[length - 2];
    return path[length - 1] == 'p' || path[length - 1] == 'P'
        ? a == '.' && b == 't' && c == 'm'
        : path[length - 1] == 'k' || path[length - 1] == 'K'
            ? a == '.' && b == 'b' && c == 'a'
            : false;
}

constexpr SdPathError validate_sd_path(const char *path)
{
    if (path == nullptr || path[0] == '\0') return SdPathError::EMPTY;
    if (path[0] == '/' || path[0] == '\\') return SdPathError::ABSOLUTE;

    size_t length = 0;
    size_t component_start = 0;
    for (; path[length] != '\0'; ++length) {
        if (length >= 219) return SdPathError::TOO_LONG;
        const unsigned char ch = static_cast<unsigned char>(path[length]);
        if (ch < 32 || ch >= 127 || ch == ':' || ch == '\\')
            return SdPathError::INVALID_CHARACTER;
        if (ch == '/') {
            const size_t component_length = length - component_start;
            if (component_length == 0 ||
                (component_length == 1 && path[component_start] == '.') ||
                (component_length == 2 && path[component_start] == '.' &&
                 path[component_start + 1] == '.'))
                return SdPathError::INVALID_COMPONENT;
            if (path[length - 1] == ' ' || path[length - 1] == '.')
                return SdPathError::INVALID_COMPONENT;
            component_start = length + 1;
        }
    }

    const size_t component_length = length - component_start;
    if (component_length == 0 ||
        (component_length == 1 && path[component_start] == '.') ||
        (component_length == 2 && path[component_start] == '.' &&
         path[component_start + 1] == '.'))
        return SdPathError::INVALID_COMPONENT;
    if (path[length - 1] == ' ' || path[length - 1] == '.')
        return SdPathError::INVALID_COMPONENT;
    if (sd_path_reserved_suffix(path, length)) return SdPathError::RESERVED_SUFFIX;
    return SdPathError::NONE;
}

static_assert(validate_sd_path("data/movements.ndjson") == SdPathError::NONE,
              "A normal relative path must be accepted");
static_assert(validate_sd_path("") == SdPathError::EMPTY &&
              validate_sd_path("/data/file") == SdPathError::ABSOLUTE &&
              validate_sd_path("../outside") == SdPathError::INVALID_COMPONENT &&
              validate_sd_path("data/../outside") == SdPathError::INVALID_COMPONENT &&
              validate_sd_path("data//file") == SdPathError::INVALID_COMPONENT &&
              validate_sd_path("data/") == SdPathError::INVALID_COMPONENT &&
              validate_sd_path("file.") == SdPathError::INVALID_COMPONENT &&
              validate_sd_path("file ") == SdPathError::INVALID_COMPONENT &&
              validate_sd_path("drive:file") == SdPathError::INVALID_CHARACTER &&
              validate_sd_path("back\\\\slash") == SdPathError::INVALID_CHARACTER &&
              validate_sd_path("data/file.tmp") == SdPathError::RESERVED_SUFFIX &&
              validate_sd_path("data/file.BAK") == SdPathError::RESERVED_SUFFIX,
              "Traversal, empty components, and transaction names must be rejected");
