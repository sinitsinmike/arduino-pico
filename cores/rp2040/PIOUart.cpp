/*
    Tone for the Raspberry Pi Pico RP2040

    Copyright (c) 2021 Earle F. Philhower, III <earlephilhower@yahoo.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <Arduino.h>
#include "CoreMutex.h"
#include <hardware/gpio.h>
#include <pico/time.h>
#include <map>

typedef struct {
    pin_size_t pin;
    PIO pio;
    int sm;
    int databits;
    enum { none, even, odd } parity;
    int stopbits;
} PIOUART;

static std::map<int, PIOProgram*> _txMap;
static std::map<int, PIOProgram*> _rxMap;


// Duplicate a program and replace the first insn with a "set x, repl"
static pio_program_t *pio_make_uart_prog(int repl, const pio_program_t *pg) {
    pio_program_t *p = new pio_program_t;
    p->length = pg->length;
    p->origin = pg->origin;
    uint16_t *insn = (uint16_t *)malloc(p->length * 2);
    memcpy(insn, pg->instructions, p->length * 2);
    insn[0] = pio_encode_set(pio_x, repl);
    p->instructions = insn;
    return p;
}

static PIOUART *newTx, *newRx;

#include "pio_uart.pio.h"
static PIOProgram _txPgm(pio_make_uart_prog(8+0+1, &pio_tx_program));
static PIOProgram _rxPgm(pio_make_uart_prog(8*2+0*2+1*2, &pio_rx_program));

void pio_tx_init(uint8_t pin, unsigned int baud) {
    if (pin > 29) {
        DEBUGCORE("ERROR: Illegal pin in pio_tx (%d)\n", pin);
        return;
    }

    newTx = new PIOUART();
    newTx->pin = pin;
    pinMode(pin, OUTPUT);
    int off;
    if (!_txPgm.prepare(&newTx->pio, &newTx->sm, &off)) {
        DEBUGCORE("ERROR: tone unable to start, out of PIO resources\n");
        // ERROR, no free slots
        delete newTx;
        return;
    }
    pio_tx_program_init(newTx->pio, newTx->sm, off, pin);
    pio_sm_clear_fifos(newTx->pio, newTx->sm); // Remove any existing data

    // Put the divider into ISR w/o using up program space
    pio_sm_put_blocking(newTx->pio, newTx->sm, clock_get_hz(clk_sys) / baud - 2);
    pio_sm_exec(newTx->pio, newTx->sm, pio_encode_pull(false, false));
    pio_sm_exec(newTx->pio, newTx->sm, pio_encode_mov(pio_isr, pio_osr));

    // Start running!
    pio_sm_set_enabled(newTx->pio, newTx->sm, true);
}

void pio_tx_putc(char c) {
    uint32_t val = c;
    val |= 256; // Stop bit = high
    val <<= 1;  // Start bit = low
    pio_sm_put_blocking(newTx->pio, newTx->sm, val);
}

void pio_rx_init(uint8_t pin, unsigned int baud) {
    if (pin > 29) {
        DEBUGCORE("ERROR: Illegal pin in pio_rx (%d)\n", pin);
        return;
    }

    newRx = new PIOUART();
    newRx->pin = pin;
    pinMode(pin, INPUT);
    int off;
    if (!_rxPgm.prepare(&newRx->pio, &newRx->sm, &off)) {
        DEBUGCORE("ERROR: tone unable to start, out of PIO resources\n");
        // ERROR, no free slots
        delete newRx;
        return;
    }
    pio_rx_program_init(newRx->pio, newRx->sm, off, pin);
    pio_sm_clear_fifos(newRx->pio, newRx->sm); // Remove any existing data

    // Put phase divider into OSR w/o using add'l program memory
    pio_sm_put_blocking(newRx->pio, newRx->sm, clock_get_hz(clk_sys) / (baud * 2) - 2);
    pio_sm_exec(newRx->pio, newRx->sm, pio_encode_pull(false, false));

    pio_sm_set_enabled(newRx->pio, newRx->sm, true);
}

int pio_rx_available() {
    return pio_sm_is_rx_fifo_empty(newRx->pio, newRx->sm) ? 0 : 1;
}

uint32_t pio_rx_getc() {
    uint32_t decode = newRx->pio->rxf[newRx->sm];
    decode >>= 14;
    uint32_t val = 0;
    for (int b = 0; b < 8; b++) {
        val |= (decode & (1<<(b*2))) ? 1<<b : 0;
    }
    return val;
}

