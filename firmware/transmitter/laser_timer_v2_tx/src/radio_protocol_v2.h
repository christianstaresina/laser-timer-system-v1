/**
 * radio_protocol_v2.h
 * Laser Timer Firmware v2 — shared nRF24 packet format and helpers
 *
 * Christian Staresina
 * 5/31/2026
 *
 * Pipe layout (addresses[][6] in each sketch):
 *   [0] "00001" — RX -> TX return channel (gate 2 complete, etc.)
 *   [1] "00002" — TX -> RX primary channel (gate 1, ping)
 */

#ifndef RADIO_PROTOCOL_V2_H
#define RADIO_PROTOCOL_V2_H

#include <SPI.h>
#include <RF24.h>

#define RADIO_MAGIC 0xA5
#define CMD_GATE1_OPEN   1
#define CMD_GATE1_CLOSED 2
#define CMD_GATE2_CLOSED 3
#define CMD_PING         0xFF

#define RADIO_CHANNEL 108
#define RADIO_HEARTBEAT_MS 500
#define RADIO_LINK_TIMEOUT_MS 2000
#define RADIO_GATE_REPEAT_MS 50
#define RADIO_DISPLAY_MS 10
#define RADIO_GATE_BURST_COUNT 3
#define RADIO_TX_SEND_PIPE 1
#define RADIO_RX_SEND_PIPE 0
#define RADIO_POWER_SETTLE_MS 500
#define RADIO_PAIR_RETRY_MS 100
#define RADIO_PAIR_OK_MS 1500
#define RADIO_PAIR_LINE0 "  Pairing...    "
#define RADIO_PAIR_LINE1 "Waiting for link"
#define RADIO_PAIR_OK0   "  Link OK!      "
#define RADIO_PAIR_OK1   "Connected       "
#define TX_COMPLETE_LINE0 "Timer complete! "

struct __attribute__((packed)) RadioPacket {
  uint8_t magic;
  uint8_t cmd;
};

extern bool radioReady;

inline bool initRadio(RF24 &radio) {
  if (!radio.begin()) {
    return false;
  }
  delay(50);

  radio.setPALevel(RF24_PA_MIN);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(RADIO_CHANNEL);
  radio.setAutoAck(true);
  radio.enableDynamicAck();
  radio.setRetries(5, 15);
  radio.setCRCLength(RF24_CRC_16);
  radio.setPayloadSize(sizeof(RadioPacket));
  delay(10);
  radio.setPALevel(RF24_PA_HIGH);
  delay(50);

  return true;
}

inline bool sendPacketOnPipe(RF24 &radio, const byte address[][6], uint8_t pipeIndex,
                             uint8_t cmd, bool requireAck = true) {
  RadioPacket pkt = {RADIO_MAGIC, cmd};
  radio.stopListening();
  radio.openWritingPipe(address[pipeIndex]);
  radio.flush_tx();

  const uint8_t maxAttempts = requireAck ? 10 : 1;
  for (uint8_t attempt = 0; attempt < maxAttempts; attempt++) {
    // RF24 write() third arg is multicast: true = NO ACK, false = wait for ACK.
    if (radio.write(&pkt, sizeof(pkt), !requireAck)) {
      return true;
    }
    if (requireAck) {
      delay(2);
    }
  }
  return false;
}

inline bool sendPacket(RF24 &radio, const byte address[][6], uint8_t cmd, bool requireAck = true) {
  return sendPacketOnPipe(radio, address, RADIO_TX_SEND_PIPE, cmd, requireAck);
}

inline bool sendToTransmitter(RF24 &radio, const byte address[][6], uint8_t cmd, bool requireAck = true) {
  return sendPacketOnPipe(radio, address, RADIO_RX_SEND_PIPE, cmd, requireAck);
}

inline void sendGateOpenBurst(RF24 &radio, const byte address[][6]) {
  for (uint8_t i = 0; i < RADIO_GATE_BURST_COUNT; i++) {
    sendPacket(radio, address, CMD_GATE1_OPEN, false);
  }
  sendPacket(radio, address, CMD_GATE1_OPEN, true);
}

inline bool sendGateClosedBurst(RF24 &radio, const byte address[][6]) {
  for (uint8_t i = 0; i < RADIO_GATE_BURST_COUNT; i++) {
    if (sendToTransmitter(radio, address, CMD_GATE2_CLOSED, true)) {
      return true;
    }
  }
  return false;
}

#endif
