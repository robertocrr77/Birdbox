#pragma once
#include <stdbool.h>
#include <stdint.h>

// Divider: GPIO14 -> 47k -> GPIO4 -> LDR -> GND.
// Nominal voltages at 3.3 V excitation; calibrate on assembled hardware.
#define LIGHT_NIGHT_MV 2245 // 100k LDR
#define LIGHT_DAY_MV 2168   // 90k LDR
#define LIGHT_CONFIRM_US 3000000LL

typedef struct {
    bool night;
    int64_t dark_since;
} light_policy_t;

static inline void light_policy_update(light_policy_t *p, bool valid, int mv, int64_t now)
{
    if (!valid) { p->dark_since = -1; return; }
    if (p->night) {
        if (mv < LIGHT_DAY_MV) p->night = false;
        p->dark_since = -1;
    } else if (mv > LIGHT_NIGHT_MV) {
        if (p->dark_since < 0) p->dark_since = now;
        if (now - p->dark_since >= LIGHT_CONFIRM_US) p->night = true;
    } else {
        p->dark_since = -1;
    }
}
