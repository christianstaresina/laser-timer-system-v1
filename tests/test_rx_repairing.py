import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RX_SRC = ROOT / "firmware" / "receiver" / "laser_timer_v2_rx" / "src"


def write_stub_headers(stub_dir: Path) -> None:
    (stub_dir / "Arduino.h").write_text(
        textwrap.dedent(
            """
            #ifndef ARDUINO_H
            #define ARDUINO_H

            #include <cstddef>
            #include <cstdint>

            using byte = uint8_t;

            #define HIGH 1
            #define LOW 0
            #define INPUT 0
            #define OUTPUT 1
            #define INPUT_PULLUP 2
            #define A0 14

            #define B00000 0b00000
            #define B00001 0b00001
            #define B00010 0b00010
            #define B00011 0b00011
            #define B00100 0b00100
            #define B00101 0b00101
            #define B00110 0b00110
            #define B01000 0b01000
            #define B01001 0b01001
            #define B01100 0b01100
            #define B01111 0b01111
            #define B10000 0b10000
            #define B10011 0b10011
            #define B10100 0b10100
            #define B11000 0b11000
            #define B11100 0b11100
            #define B11110 0b11110
            #define B11111 0b11111

            unsigned long millis();
            void delay(unsigned long ms);
            int digitalRead(int pin);
            void digitalWrite(int pin, int value);
            void pinMode(int pin, int mode);
            void tone(int pin, unsigned int frequency);
            void noTone(int pin);

            #endif
            """
        )
    )
    (stub_dir / "SPI.h").write_text(
        textwrap.dedent(
            """
            #ifndef SPI_H
            #define SPI_H

            class SPIClass {
             public:
              void begin() {}
            };

            extern SPIClass SPI;

            #endif
            """
        )
    )
    (stub_dir / "RF24.h").write_text(
        textwrap.dedent(
            """
            #ifndef RF24_H
            #define RF24_H

            #include "Arduino.h"
            #include <array>
            #include <cstring>
            #include <deque>

            #define RF24_PA_MIN 0
            #define RF24_PA_HIGH 3
            #define RF24_250KBPS 0
            #define RF24_CRC_16 0

            class RF24 {
             public:
              RF24(byte cePin, byte csPin) {}

              bool begin() { return true; }
              void setPALevel(int level) {}
              void setDataRate(int rate) {}
              void setChannel(int channel) {}
              void setAutoAck(bool enabled) {}
              void setRetries(byte delay, byte count) {}
              void setCRCLength(int length) {}
              void setPayloadSize(std::size_t size) {}
              void openReadingPipe(byte pipe, const byte* address) {}
              void openWritingPipe(const byte* address) {}
              void startListening() { listening = true; }
              void stopListening() { listening = false; }
              void flush_tx() {}
              void flush_rx() { rxQueue.clear(); }
              bool write(const void* data, std::size_t len, bool multicast = false) {
                return true;
              }
              bool available() { return !rxQueue.empty(); }
              void read(void* data, std::size_t len) {
                Payload payload = rxQueue.front();
                rxQueue.pop_front();
                std::memcpy(data, payload.bytes.data(), len < payload.len ? len : payload.len);
              }
              void pushPacket(uint8_t magic, uint8_t cmd) {
                Payload payload{};
                payload.len = 2;
                payload.bytes[0] = magic;
                payload.bytes[1] = cmd;
                rxQueue.push_back(payload);
              }

              bool listening = false;

             private:
              struct Payload {
                std::array<uint8_t, 32> bytes{};
                std::size_t len = 0;
              };

              std::deque<Payload> rxQueue;
            };

            #endif
            """
        )
    )
    (stub_dir / "LiquidCrystal_I2C.h").write_text(
        textwrap.dedent(
            """
            #ifndef LIQUID_CRYSTAL_I2C_H
            #define LIQUID_CRYSTAL_I2C_H

            #include "Arduino.h"

            class LiquidCrystal_I2C {
             public:
              LiquidCrystal_I2C(int address, int columns, int rows) {}
              void init() {}
              void backlight() {}
              void begin(int columns, int rows) {}
              void clear() {}
              void setCursor(int column, int row) {}
              void createChar(byte index, byte data[]) {}
              void print(const char* value) {}
              void print(char value) {}
              void print(float value, int digits) {}
            };

            #endif
            """
        )
    )


class RxRePairingTest(unittest.TestCase):
    def test_repairing_completes_after_main_loop_drains_radio_packet(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            write_stub_headers(tmp_path)
            source = tmp_path / "rx_repairing_harness.cpp"
            binary = tmp_path / "rx_repairing_harness"
            source.write_text(
                textwrap.dedent(
                    f"""
                #include <cstdlib>
                #include <iostream>

                #include "Arduino.h"
                #include "SPI.h"
                #include "LiquidCrystal_I2C.h"

                unsigned long fakeMillis = 1000;
                unsigned long millis() {{ return fakeMillis; }}
                void delay(unsigned long ms) {{ fakeMillis += ms; }}
                int digitalRead(int pin) {{ return LOW; }}
                void digitalWrite(int pin, int value) {{}}
                void pinMode(int pin, int mode) {{}}
                void tone(int pin, unsigned int frequency) {{}}
                void noTone(int pin) {{}}

                SPIClass SPI;
                LiquidCrystal_I2C lcd(0x27, 16, 2);

                #include "{RX_SRC / 'functions_laser_timer_v2_rx.h'}"

                char distance = ENABLED;
                int distance_in_yards = 40;
                int custom_distance_yards = 40;
                bool buzzer_align_enabled = true;
                bool buzzer_finish_enabled = true;
                bool use_meters = false;

                void loadSettings() {{}}
                void saveSettings() {{}}
                void encoderInit() {{}}
                EncoderEvent nextEncoderEvent = EncNone;
                EncoderEvent pollEncoder() {{
                  EncoderEvent event = nextEncoderEvent;
                  nextEncoderEvent = EncNone;
                  return event;
                }}
                void waitForEncoderRelease() {{}}

                #include "{RX_SRC / 'display_rx.cpp'}"
                #include "{RX_SRC / 'menu_rx.cpp'}"

                static void require(bool condition, const char* message) {{
                  if (!condition) {{
                    std::cerr << message << std::endl;
                    std::exit(1);
                  }}
                }}

                int main() {{
                  radioReady = true;
                  rxAwaitingFirstLink = false;
                  menuInit();

                  radioPacketSequence = 10;
                  actionRadioSelect(RM_RePair);
                  menuTick();
                  require(menuScreen == MS_RePairing,
                          "re-pair should keep waiting when no fresh packet was observed");

                  radio.pushPacket(0x00, CMD_PING);
                  PollRadio();
                  menuTick();
                  require(menuScreen == MS_RePairing,
                          "invalid radio packets must not complete re-pairing");

                  radio.pushPacket(RADIO_MAGIC, CMD_PING);
                  PollRadio();
                  require(!radio.available(),
                          "top-level PollRadio should drain the queued packet before menuTick");

                  menuTick();
                  require(menuScreen == MS_Splash,
                          "re-pair should complete after a fresh valid packet was drained");
                  require(splashNextScreen == MS_Main,
                          "re-pair success splash should return to the main menu");
                  require(splashUntilMs == fakeMillis + RADIO_PAIR_OK_MS,
                          "re-pair success splash should use the configured display duration");

                  return 0;
                }}
                """
                )
            )
            subprocess.run(
                [
                    "g++",
                    "-std=c++17",
                    f"-I{tmp_path}",
                    f"-I{RX_SRC}",
                    str(source),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
