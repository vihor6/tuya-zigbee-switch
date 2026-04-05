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
    hal_zigbee_clear_binding_table();
}
