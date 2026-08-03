import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RX_ROOT = ROOT / "firmware" / "receiver" / "laser_timer_v2_rx"
TX_ROOT = ROOT / "firmware" / "transmitter" / "laser_timer_v2_tx"


def read_source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


class FirmwareRegressionTest(unittest.TestCase):
    def test_encoder_release_handling_does_not_block_timing_loops(self) -> None:
        rx_encoder = read_source(RX_ROOT / "src" / "encoder_rx.cpp")
        tx_functions = read_source(TX_ROOT / "src" / "functions_laser_timer_v2_tx.h")

        blocking_release = "while (digitalRead(encoderButton) == LOW)"
        self.assertNotIn(blocking_release, rx_encoder)
        self.assertNotIn(blocking_release, tx_functions)
        self.assertIn("suppressUntilRelease", rx_encoder)

    def test_transmitter_completion_display_is_non_blocking(self) -> None:
        tx_functions = read_source(TX_ROOT / "src" / "functions_laser_timer_v2_tx.h")

        self.assertNotIn("while (millis() < completeUntil)", tx_functions)
        self.assertIn("txCompleteUntilMs", tx_functions)
        self.assertRegex(
            tx_functions,
            re.compile(r"if \(txCompleteUntilMs != 0 && now >= txCompleteUntilMs\)"),
        )

    def test_required_rf_packets_request_acks(self) -> None:
        headers = [
            ROOT / "firmware" / "common" / "radio_protocol_v2.h",
            RX_ROOT / "src" / "radio_protocol_v2.h",
            TX_ROOT / "src" / "radio_protocol_v2.h",
        ]

        for header in headers:
            with self.subTest(header=header):
                source = read_source(header)
                self.assertIn("radio.enableDynamicAck();", source)
                self.assertIn("radio.write(&pkt, sizeof(pkt), !requireAck)", source)
                self.assertIn("sendGateClosedBurst", source)

    def test_receiver_repairing_screen_owns_radio_reads(self) -> None:
        rx_loop = read_source(RX_ROOT / "laser_timer_v2_rx.ino")
        rx_menu = read_source(RX_ROOT / "src" / "menu_rx.cpp")
        repairing_start = rx_menu.index("static void tickRePairing() {")
        repairing_end = rx_menu.index("\n}\n\nvoid menuInit()", repairing_start)
        repairing_body = rx_menu[repairing_start:repairing_end]

        self.assertIn("if (!menuIsRePairing())", rx_loop)
        self.assertNotIn("PollRadio();", repairing_body)
        self.assertIn("while (radio.available())", repairing_body)

    def test_starting_stopwatch_resets_stale_run_state(self) -> None:
        rx_functions = read_source(RX_ROOT / "src" / "functions_laser_timer_v2_rx.h")
        rx_menu = read_source(RX_ROOT / "src" / "menu_rx.cpp")

        self.assertIn("void resetStopwatchRunState()", rx_functions)
        self.assertIn("gate2.timer_state = OFF;", rx_functions)
        self.assertIn("startMillis = 0;", rx_functions)
        self.assertLess(
            rx_menu.index("resetStopwatchRunState();"),
            rx_menu.index("timer_state = ENABLED;"),
        )


if __name__ == "__main__":
    unittest.main()
