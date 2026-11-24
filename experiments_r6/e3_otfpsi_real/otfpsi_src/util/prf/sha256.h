#include <stdint.h>

#ifdef ENABLE_SHA

extern uint32_t sha256_initial_state[8];
void sha256_process_x86(uint32_t state[8], const uint8_t data[], uint32_t length);

#endif
