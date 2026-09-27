"""Prove della logica dell'app ArduLearn (senza schede collegate):  python -m unittest tools/app/test_app.py"""
import os
import sys
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ardulearn_app as app  # noqa: E402


class Riconoscimento(unittest.TestCase):
    def test_r4_e_modulo_in_download(self):
        self.assertEqual(app.classifica_porta(0x2341, 0x1002)[1], app.R4_WIFI)
        self.assertEqual(app.classifica_porta(0x303A, 0x1001)[1], app.R4_WIFI)   # ESP32 da ripristinare
        self.assertEqual(app.classifica_porta(0x2341, 0x0042)[1], app.MEGA)

    def test_versioni(self):
        self.assertEqual(app.versione_tupla("0.3.1"), (0, 3, 1))
        self.assertGreater(app.versione_tupla("0.10.0"), app.versione_tupla("0.9.9"))


class ModuloWifi(unittest.TestCase):
    """Cosa scrive l'app sul modulo Wi-Fi secondo il firmware che trova."""

    def prova(self, letto, rom=None, incluso="0.3.1"):
        comandi = []

        def esegui(cmd, cartella, scrivi, timeout=None):
            comandi.append(cmd)
            return 0
        with mock.patch.object(app, "leggi_versioni", return_value={"bridge": incluso}), \
                mock.patch.object(app, "porta_modulo_in_download", return_value=rom), \
                mock.patch.object(app, "leggi_modulo_wifi", return_value=letto), \
                mock.patch.object(app, "modulo_in_download", return_value="COM9"), \
                mock.patch.object(app, "trova_firmware", side_effect=lambda n: os.path.join("fw", n)), \
                mock.patch.object(app, "porta_r4", return_value="COM3"), \
                mock.patch.object(app.time, "sleep"):
            porta = app.installa_modulo_wifi(lambda t: None, esegui)
        return porta, comandi

    def test_aggiornato_niente_da_fare(self):
        porta, comandi = self.prova(((0, 3, 1), True))
        self.assertEqual(porta, "COM3")
        self.assertEqual(comandi, [])

    def test_vecchio_solo_app(self):
        porta, comandi = self.prova(((0, 3, 0), True))
        self.assertEqual(porta, "COM3")
        self.assertEqual(len(comandi), 1)
        self.assertIn("ArduLearnBridge.bin", comandi[0])
        self.assertNotIn("ArduLearnBridge_littlefs.bin", comandi[0])   # slot e bozza restano
        self.assertIn("0xe000", comandi[0])

    def test_firmware_arduino_tutto(self):
        porta, comandi = self.prova(((0, 6, 0), False))                # firmware originale 0.6.0
        self.assertEqual(len(comandi), 1)
        for nome in ("ArduLearnBridge_bootloader.bin", "ArduLearnBridge_partitions.bin",
                     "ArduLearnBridge.bin", "ArduLearnBridge_littlefs.bin"):
            self.assertIn(nome, comandi[0])
        self.assertIn("COM9", comandi[0])

    def test_modulo_in_download_tutto(self):
        porta, comandi = self.prova(None, rom="COM9")
        self.assertIn("ArduLearnBridge_littlefs.bin", comandi[0])

    def test_versione_illeggibile(self):
        porta, comandi = self.prova(None)
        self.assertIsNone(porta)
        self.assertEqual(comandi, [])


class CaricamentoR4(unittest.TestCase):
    def test_sequenza(self):
        passi = []
        ok = app.esegui_caricamento(
            app.R4_WIFI, "COM3", os.path.join("fw", "PlcBlocchi_r4wifi.bin"), lambda t: None,
            esegui=lambda cmd, c, s, timeout=None: passi.append(os.path.basename(cmd[0])) or 0,
            tocco=lambda p, s: passi.append("tocco " + p), attesa=lambda s: None,
            attendi=lambda p, s: True, modulo=lambda s, e: passi.append("modulo") or "COM5")
        self.assertTrue(ok)
        self.assertEqual(passi, ["modulo", "tocco COM5", "bossac.exe"])   # porta nuova dopo il modulo

    def test_modulo_non_riuscito(self):
        ok = app.esegui_caricamento(app.R4_WIFI, "COM3", "x.bin", lambda t: None,
                                    esegui=lambda *a, **k: self.fail("non deve caricare il PLC"),
                                    modulo=lambda s, e: None)
        self.assertFalse(ok)


class Pagina(unittest.TestCase):
    def test_r4_con_bridge_come_il_mega(self):
        info = {"board": "r4wifi", "fs": 1, "sd": 1, "pv": 100}
        self.assertFalse(app.pagina_nel_firmware(info))
        self.assertEqual(app.stato_pagina(info, 200)[0], "da aggiornare")
        self.assertEqual(app.stato_pagina(info, 100)[0], "aggiornata")
        self.assertTrue(app.pagina_nel_firmware({"board": "r4wifi"}))   # vecchio firmware R4


if __name__ == "__main__":
    unittest.main()
