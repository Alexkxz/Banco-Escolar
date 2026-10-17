#pragma once

// Lab-only bootstrap. In a normal build this function is an inert no-op.
// In a lab build it returns only for a valid PENDING_VERIFY boot; otherwise it
// waits for an explicit local operator command or remains fail-closed.
void ota_lab_bootstrap_before_services(bool ota_manager_ready, bool manager_detected_pending);
