"""Contracts for the single normal firmware OTA profile and archived lab hooks."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class OtaFirmwareProfileContractTests(unittest.TestCase):
    def test_normal_profile_has_no_laboratory_bootstrap_or_installer_dependencies(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
        config = (ROOT / "src/network/ota_config.h").read_text(encoding="utf-8")
        source = ROOT / "src/network"

        for marker in ("ota_lab_bootstrap", "ota_lab_installer"):
            self.assertNotIn(marker, main)
            self.assertEqual(list(source.glob(f"{marker}.*")), [])
        self.assertNotIn("BANCO_OTA_LAB", platformio)
        self.assertNotIn("BANCO_OTA_LAB", config)
        self.assertIn("-DBANCO_OTA_ENABLED=0", platformio)
        self.assertTrue((ROOT / "tools/ota/lab/archive/README_ARCHIVE.md").is_file())

    def test_normal_boot_partition_lookup_uses_subtypes_and_geometry_not_ota_labels(self):
        ota = (ROOT / "src/network/ota_manager.cpp").read_text(encoding="utf-8")
        geometry = ota.split("bool normal_profile_has_expected_layout(", 1)[1].split(
            "bool https_url(", 1)[0]
        self.assertIn("ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr", geometry)
        self.assertIn("ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr", geometry)
        self.assertIn("running->address != OTA0_ADDRESS", geometry)
        self.assertIn("running->size != OTA_SLOT_SIZE", geometry)
        self.assertIn("ota1->address != OTA1_ADDRESS", geometry)
        self.assertIn("ota1->size != OTA_SLOT_SIZE", geometry)
        self.assertIn("otadata->address != OTADATA_ADDRESS", geometry)
        self.assertIn("filesystem->address != FILESYSTEM_ADDRESS", geometry)

    def test_safe_normal_boot_keeps_wifi_and_demo_entry_paths_reachable(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        setup = main.split("void setup()", 1)[1].split("void loop()", 1)[0]
        loop = main.split("void loop()", 1)[1]
        demo = main.split("void demo_entry_event(", 1)[1].split("void format_uid(", 1)[0]

        # The safe normal OTA state is released by OtaManager::begin() before
        # these existing services start; these assertions ensure cleanup did
        # not detach Wi-Fi or remove the DEMO entry point.
        self.assertIn("ota_boot_validation_hold = !ota_manager_ready_at_boot || ota_manager.bootValidationHoldRequired();", setup)
        self.assertIn("ota_maintenance_active.store(ota_boot_validation_hold);", setup)
        self.assertIn("wifi_manager.begin();", setup)
        self.assertIn("if (!ota_maintenance_active.load())", loop)
        self.assertIn("wifi_manager.update();", loop)
        self.assertIn('"ENTRAR A DEMO"', main)
        self.assertIn("if (ota_maintenance_active.load()) return;", demo)
        self.assertIn("storage_manager.createAccountIfMissing", demo)
        self.assertIn("mostrar_pantalla(PANTALLA_ALUMNO);", demo)


if __name__ == "__main__":
    unittest.main(verbosity=2)
