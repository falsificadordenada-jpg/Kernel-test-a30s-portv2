#ifndef _A30S_INPUT_H
#define _A30S_INPUT_H
/* Botoes (gpa1 @0x11CB0060, pinos 5/6/7) e touch Imagis (I2C4 @0x13870000, addr 0x50)
 * do Galaxy A30s. Portado dos drivers bare-metal testados no aparelho (m3.c / m3b.c).
 * Thread de polling a cada 20 ms; eventos saem por printf (console do kernel). */
extern void a30s_input_start(void);
#endif
