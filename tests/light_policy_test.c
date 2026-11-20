#include "../main/light_policy.h"
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

// Freestanding host executable: returns the failed source line, or zero.
int mainCRTStartup(void)
{
    light_policy_t p = {.dark_since = -1};
    light_policy_update(&p, true, 428, 0); // User's ~7k room-light condition.
    CHECK(!p.night && p.dark_since == -1);
    light_policy_update(&p, true, 2151, 1000000); // User's ~88k hallway condition.
    CHECK(!p.night);
    light_policy_update(&p, true, 2300, 2000000);
    light_policy_update(&p, true, 2300, 4999999);
    CHECK(!p.night); // A shadow shorter than three seconds must not stop audio.
    light_policy_update(&p, true, 2151, 5000000);
    CHECK(p.dark_since == -1);
    light_policy_update(&p, true, 2300, 6000000);
    light_policy_update(&p, false, 2300, 8000000);
    CHECK(!p.night && p.dark_since == -1); // Invalid readings cannot confirm dark.
    light_policy_update(&p, true, 2300, 9000000);
    light_policy_update(&p, true, 2300, 12000000);
    CHECK(p.night);
    light_policy_update(&p, true, 2200, 13000000);
    CHECK(p.night); // Hysteresis band must not wake a dark box.
    light_policy_update(&p, true, LIGHT_DAY_MV, 14000000);
    CHECK(p.night); // Strictly below the daylight threshold is required.
    light_policy_update(&p, false, 428, 15000000);
    CHECK(p.night);
    light_policy_update(&p, true, 2151, 16000000);
    CHECK(!p.night);
    light_policy_update(&p, true, LIGHT_NIGHT_MV, 17000000);
    CHECK(!p.night && p.dark_since == -1);
    // Fresh timer-wake state retains night but discards old uptime timestamps.
    p = (light_policy_t){.night = true, .dark_since = -1};
    light_policy_update(&p, true, 2200, 0);
    CHECK(p.night);
    light_policy_update(&p, true, 2151, 50000);
    CHECK(!p.night);
    return 0;
}
