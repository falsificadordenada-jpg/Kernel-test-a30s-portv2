#ifndef _A30S_INPUT_H
#define _A30S_INPUT_H
/*
 * Galaxy A30s input: hardware buttons (gpa1 @0x11CB0060, pins 5/6/7) and the
 * Imagis touch controller (I2C4 @0x13870000, address 0x50).
 *
 * Ported from the bare-metal drivers tested on the device (m3.c / m3b.c).
 * A kernel thread polls every 20 ms; events are reported with printf (kernel console).
 *
 * Boot-args (none are required):
 *   a30s_input=0       disable buttons and touch completely
 *   a30s_tp=0          disable only the touch controller
 *   a30s_tp_msg=<reg>  interrupt-message register   (default 0xb00b0104)
 *   a30s_tp_coord=<reg> first coordinate register   (default 0xb00b0108)
 *   a30s_tp_irq=1      read the chip only while its IRQ pin (gpa0-0) is active
 *   a30s_tp_dump=<n>   dump touch words every n polls (0 = off), to map the layout
 */
extern void a30s_input_start(void);
#endif
