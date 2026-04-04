#ifdef SL_COMPONENT_CATALOG_PRESENT
#include "sl_component_catalog.h"
#endif
#include "sl_system_init.h"
#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
#include "sl_power_manager.h"
#endif
#if defined(SL_CATALOG_KERNEL_PRESENT)
#include "sl_system_kernel.h"
#else
#include "sl_system_process_action.h"
#endif // SL_CATALOG_KERNEL_PRESENT

#include "app/framework/include/af.h"
#include "em_gpio.h"

#include "app.h"
#include "base_components/button.h"
#include "base_components/led.h"
#include "base_components/network_indicator.h"
#include "base_components/relay.h"
#include "device_config/config_nv.h"
#include "device_config/config_parser.h"
#include "hal/gpio.h"
#include "hal/timer.h"
#include "hal/zigbee.h"
#include "zigbee/basic_cluster.h"
#include "zigbee/consts.h"
#include "zigbee/general_commands.h"
#include "zigbee/relay_cluster.h"
#include "zigbee/switch_cluster.h"

// TODO: make configurable via ZCL
#define POLLING_INTERVAL_MS    100

#if defined(_SILICON_LABS_32B_SERIES_1)
uint32_t emberAfOtaStorageDriverRetrieveLastStoredOffsetCallback(void);
EmberAfOtaStorageStatus emberAfOtaStorageClearTempDataCallback(void);
void emberAfSetShortPollIntervalMsCallback(uint16_t shortPollIntervalMs);
void emberAfSetLongPollIntervalMsCallback(uint32_t longPollIntervalMs);
#endif

static uint32_t silabs_ota_retrieve_last_stored_offset(void) {
#if defined(_SILICON_LABS_32B_SERIES_1)
    return emberAfOtaStorageDriverRetrieveLastStoredOffsetCallback();
#else
    return sl_zigbee_af_ota_storage_driver_retrieve_last_stored_offset_cb();
#endif
}

static void silabs_ota_clear_temp_data(void) {
#if defined(_SILICON_LABS_32B_SERIES_1)
    (void)emberAfOtaStorageClearTempDataCallback();
#else
    sl_zigbee_af_ota_storage_clear_temp_data_cb();
#endif
}

static void silabs_set_startup_poll_intervals(uint32_t poll_ms) {
#if defined(_SILICON_LABS_32B_SERIES_1)
    emberAfSetShortPollIntervalMsCallback((uint16_t)poll_ms);
    emberAfSetLongPollIntervalMsCallback(poll_ms);
#else
    sl_zigbee_af_set_short_poll_interval_ms_cb(poll_ms);
    sl_zigbee_af_set_long_poll_interval_ms_cb(poll_ms);
#endif
}

void drop_old_ota_image_if_any() {
    // Drop old OTA image if any exists
    // Allows to re-download FORCE image multiple times
    uint32_t currentOffset = silabs_ota_retrieve_last_stored_offset();

    if (currentOffset > 0) {
        printf("Dropping old OTA image, current offset: %lu\n", currentOffset);
        silabs_ota_clear_temp_data();
    }
}

int main(void) {
    // Initialize Silicon Labs device, system, service(s) and protocol stack(s).
    // Note that if the kernel is present, processing task(s) will be created by
    // this call.
    sl_system_init();

    app_init();

    drop_old_ota_image_if_any();

    // Switch should never "long poll", as it should always be somewhat reactive
    // to ZCL commands.
    silabs_set_startup_poll_intervals(POLLING_INTERVAL_MS);

#if defined(SL_CATALOG_KERNEL_PRESENT)
    // Start the kernel. Task(s) created in app_init() will start running.
    sl_system_kernel_start();
#else // SL_CATALOG_KERNEL_PRESENT
    while (1) {
        // Do not remove this call: Silicon Labs components process action routine
        // must be called from the super loop.
        sl_system_process_action();

        // Application process.
        app_task();

        // Let the CPU go to sleep if the system allow it.
#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
        sl_power_manager_sleep();
#endif // SL_CATALOG_POWER_MANAGER_PRESENT
    }
#endif // SL_CATALOG_KERNEL_PRESENT

    return 0;
}
