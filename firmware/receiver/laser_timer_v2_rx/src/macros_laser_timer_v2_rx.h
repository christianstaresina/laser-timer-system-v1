/**
 * macros_laser_timer_v2_rx.h
 * Laser Timer Firmware v2 — receiver constants and macros
 *
 * Christian Staresina
 * 5/31/2026
 */

#ifndef MACROS_LASER_TIMER_V2_RX_H
#define MACROS_LASER_TIMER_V2_RX_H

#define ON  ('I')
#define OFF ('O')

#define OPENED ('P')
#define CLOSED ('C')
#define ENABLED ('E')
#define DISABLED ('D')

// Gate 2: HIGH when the laser beam is broken (athlete has crossed)
#define GATE_ACTIVATED (HIGH)

#endif
