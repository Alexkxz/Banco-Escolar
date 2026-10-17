#pragma once

// Runs only in the isolated installer build and always before application
// services. All other builds link an inert implementation.
void ota_lab_installer_before_services(bool ota_manager_ready);
