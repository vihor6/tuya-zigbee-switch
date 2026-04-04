#include "hal/system.h"
#include "app/framework/include/af.h"
#include "hal/zigbee.h"
#include "micro-common.h"
#include <stdbool.h>
#include <stdint.h>

void hal_system_reset(void) {
    halReboot();
}

void hal_factory_reset(void) {
    hal_zigbee_leave_network();
#if !defined(_SILICON_LABS_32B_SERIES_1)
    sl_zigbee_clear_binding_table();
#endif
}
