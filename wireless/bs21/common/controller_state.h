#ifndef CONTROLLER_STATE_H
#define CONTROLLER_STATE_H

#include <stdint.h>

#define BIT(n) (1UL << (n))

/* Button bitmask definitions */
#define BTN_A       BIT(0)
#define BTN_B       BIT(1)
#define BTN_X       BIT(2)
#define BTN_Y       BIT(3)
#define BTN_LB      BIT(4)
#define BTN_RB      BIT(5)
#define BTN_BACK    BIT(6)
#define BTN_START   BIT(7)
#define BTN_GUIDE   BIT(8)
#define BTN_L3      BIT(9)
#define BTN_R3      BIT(10)
#define BTN_DUP     BIT(11)
#define BTN_DDOWN   BIT(12)
#define BTN_DLEFT   BIT(13)
#define BTN_DRIGHT  BIT(14)
/* bit 15 reserved */

/* Structured controller state - standard interface between layers */
struct controller_state {
    uint16_t buttons;      /* Button bitmask (see BTN_* above) */
    uint8_t  lt;           /* Left trigger  0-255 */
    uint8_t  rt;           /* Right trigger 0-255 */
    int16_t  lx;           /* Left stick X  -32768 ~ 32767 */
    int16_t  ly;           /* Left stick Y */
    int16_t  rx;           /* Right stick X */
    int16_t  ry;           /* Right stick Y */
    uint8_t  battery;      /* Battery level 0-100 */
};

#endif /* CONTROLLER_STATE_H */