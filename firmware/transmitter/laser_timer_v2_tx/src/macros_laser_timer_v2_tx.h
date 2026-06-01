/**
 * macros_laser_timer_v2_tx.h
 * Laser Timer Firmware v2 — transmitter constants and macros
 *
 * Christian Staresina
 * 5/31/2026
 */

#ifndef MACROS_LASER_TIMER_V2_TX_H
#define MACROS_LASER_TIMER_V2_TX_H

#define ON  ('I')
#define OFF ('O')

#define OPENED ('P')
#define CLOSED ('C')
#define ENABLED ('E')
#define DISABLED ('D')

// Gate 1: HIGH when the laser beam is broken (athlete has crossed)
#define GATE_ACTIVATED (HIGH)

#endif
