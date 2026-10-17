#pragma once

#include <atomic>

// Shared fail-closed gate for school-data writers. On a pending OTA boot this
// is reconstructed from persistent otadata before storage or UI startup.
inline std::atomic<bool> ota_maintenance_active{false};

