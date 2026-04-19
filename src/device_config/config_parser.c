#include "hal/gpio.h"
#include "hal/printf_selector.h"
#include "hal/zigbee.h"
#include "zigbee/basic_cluster.h"
#include "zigbee/battery_cluster.h"
#include "zigbee/consts.h"
#include "zigbee/cover_cluster.h"
#include "zigbee/cover_switch_cluster.h"
#include "zigbee/group_cluster.h"
#include "zigbee/relay_cluster.h"
#include "zigbee/poll_control_cluster.h"
#include "zigbee/switch_cluster.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "base_components/led.h"
#include "base_components/network_indicator.h"
#include "base_components/battery.h"
#include "config_nv.h"
#include "device_config/device_params_nv.h"
#include "device_config/reset.h"
#include "hal/system.h"
#include "hal/zigbee.h"
#include "hal/zigbee_ota.h"

// Forward declarations
void peripherals_init(void);

#define ARRAY_LEN(arr)    (sizeof(arr) / sizeof((arr)[0]))

// extern ota_preamble_t baseEndpoint_otaInfo;

network_indicator_t network_indicator = {
    .leds                        = { NULL, NULL, NULL, NULL },
    .has_dedicated_led           = 0,
    .manual_state_when_connected = 1,
};

led_t   leds[5];
uint8_t leds_cnt = 0;

button_t buttons[11];
uint8_t  buttons_cnt = 0;

relay_t relays[10]; // 4 relay endpoints + 3 cover endpoints
uint8_t relays_cnt = 0;

zigbee_basic_cluster basic_cluster = {
    .deviceEnable = 1,
};

zigbee_group_cluster group_cluster = {};

zigbee_switch_cluster switch_clusters[4];
uint8_t switch_clusters_cnt = 0;

zigbee_relay_cluster relay_clusters[4];
uint8_t relay_clusters_cnt = 0;

zigbee_cover_switch_cluster cover_switch_clusters[3];
uint8_t cover_switch_clusters_cnt = 0;

zigbee_cover_cluster cover_clusters[3];
uint8_t cover_clusters_cnt = 0;

hal_zigbee_cluster  clusters[32];
hal_zigbee_endpoint endpoints[10];

uint8_t allow_simultaneous_latching_pulses = 0;

battery_t battery = {
    .pin         = HAL_INVALID_PIN,
    .voltage_min =            2000,
    .voltage_max =            3000,
};

uint32_t parse_int(const char *s);
char *seek_until(char *cursor, char needle);
char *extract_next_entry(char **cursor);

typedef struct {
    uint8_t leds;
    uint8_t buttons;
    uint8_t relay_outputs;
    uint8_t relay_endpoints;
    uint8_t switch_endpoints;
    uint8_t cover_switch_endpoints;
    uint8_t cover_endpoints;
    bool    has_battery;
} device_config_usage_t;

static const char *find_pin_tail(const char *cursor);
static hal_gpio_pin_t parse_entry_pin(const char *cursor, const char **tail);
static hal_gpio_pull_t parse_entry_pull(const char *cursor);
static uint8_t entry_pressed_when_high(hal_gpio_pull_t pull);
static uint8_t entry_pin_on_high(const char *cursor);
static bool device_config_copy_parse_buffer(char *dst, size_t dst_size);
static bool device_config_validate_buffer(char *buffer,
                                          device_config_usage_t *usage);
static bool device_config_validate_current(device_config_usage_t *usage);

void on_reset_clicked(void *_) {
    hal_factory_reset();
}

void on_multi_press_reset(void *_, uint8_t press_count) {
    if (g_multi_press_reset_count != 0 &&
        press_count >= g_multi_press_reset_count) {
        hal_factory_reset();
    }
}

bool device_config_validate(void) {
    return device_config_validate_current(NULL);
}

static const char *find_pin_tail(const char *cursor) {
    const char *pin_start = cursor;

    if (!cursor || cursor[0] < 'A' || cursor[0] > 'F') {
        return cursor;
    }

    cursor++;
    if (*cursor < '0' || *cursor > '9') {
        return pin_start;
    }

    cursor++;
    if (*cursor >= '0' && *cursor <= '9') {
        cursor++;
    }

    if (*cursor >= '0' && *cursor <= '9') {
        return pin_start;
    }

    return cursor;
}

static hal_gpio_pin_t parse_entry_pin(const char *cursor, const char **tail) {
    const char *pin_tail = find_pin_tail(cursor);
    size_t      pin_len  = (size_t)(pin_tail - cursor);

    if (pin_len == 0 || pin_len >= 4) {
        if (tail != NULL) {
            *tail = cursor;
        }
        return HAL_INVALID_PIN;
    }

    char pin_buf[4];
    memcpy(pin_buf, cursor, pin_len);
    pin_buf[pin_len] = '\0';

    if (tail != NULL) {
        *tail = pin_tail;
    }

    return hal_gpio_parse_pin(pin_buf);
}

static hal_gpio_pull_t parse_entry_pull(const char *cursor) {
    if (cursor == NULL || cursor[0] == '\0') {
        return HAL_GPIO_PULL_NONE;
    }

    return hal_gpio_parse_pull(cursor);
}

static uint8_t entry_pressed_when_high(hal_gpio_pull_t pull) {
    return pull == HAL_GPIO_PULL_DOWN;
}

static uint8_t entry_pin_on_high(const char *cursor) {
    return cursor == NULL || cursor[0] != 'i';
}

void parse_config() {
    device_config_read_from_nv();
    char parse_buf[sizeof(device_config_str.data) + 1];
    device_config_usage_t usage;

    if (!device_config_validate_current(&usage)) {
        printf("Device configuration in NVM is invalid\r\n");
        reset_all();
    }

    if (!device_config_copy_parse_buffer(parse_buf, sizeof(parse_buf))) {
        printf("Device configuration could not be copied for parsing\r\n");
        reset_all();
    }

    char *cursor = parse_buf;

    const char *zb_manufacturer = extract_next_entry(&cursor);

    basic_cluster.manuName[0] = strlen(zb_manufacturer);
    if (basic_cluster.manuName[0] > 31) {
        printf("Manufacturer too big\r\n");
        reset_all();
    }
    memcpy(basic_cluster.manuName + 1, zb_manufacturer,
           basic_cluster.manuName[0]);

    const char *zb_model = extract_next_entry(&cursor);
    basic_cluster.modelId[0] = strlen(zb_model);
    if (basic_cluster.modelId[0] > 31) {
        printf("Model too big\r\n");
        reset_all();
    }
    memcpy(basic_cluster.modelId + 1, zb_model, basic_cluster.modelId[0]);

    bool     has_dedicated_status_led = false;
    uint16_t debounce_ms = DEBOUNCE_DELAY_MS;
    char *   entry;
    for (entry = extract_next_entry(&cursor); *entry != '\0';
         entry = extract_next_entry(&cursor)) {
        if (entry[0] == 'S' && entry[1] == 'L' && entry[2] == 'P') {
            // Simultaneous Latching Pulses == SLP
            allow_simultaneous_latching_pulses = 1;
        } else if (entry[0] == 'D' && entry[1] >= '0' && entry[1] <= '9') {
            // D<N> sets the global debounce duration in milliseconds.
            debounce_ms = (uint16_t)parse_int(entry + 1);
            for (int i = 0; i < buttons_cnt; i++) {
                buttons[i].debounce_delay_ms = debounce_ms;
            }
        } else if (entry[0] == 'B' && entry[1] == 'T') {
            // Battery: BT<pin>, e.g. BTC5
            hal_gpio_pin_t pin = parse_entry_pin(entry + 2, NULL);
            battery.pin = pin;
            battery_init(&battery);
        } else if (entry[0] == 'B') {
            const char *    tail = NULL;
            hal_gpio_pin_t  pin  = parse_entry_pin(entry + 1, &tail);
            hal_gpio_pull_t pull = parse_entry_pull(tail);
            hal_gpio_init(pin, 1, pull);

            buttons[buttons_cnt].pin = pin;
            buttons[buttons_cnt].long_press_duration_ms  = 2000;
            buttons[buttons_cnt].multi_press_duration_ms = 800;
            buttons[buttons_cnt].debounce_delay_ms       = debounce_ms;
            buttons[buttons_cnt].pressed_when_high       =
                entry_pressed_when_high(pull);
            buttons[buttons_cnt].on_long_press = on_reset_clicked;
            buttons_cnt++;
        } else if (entry[0] == 'L') {
            const char *   tail = NULL;
            hal_gpio_pin_t pin  = parse_entry_pin(entry + 1, &tail);
            hal_gpio_init(pin, 0, HAL_GPIO_PULL_NONE);
            leds[leds_cnt].pin     = pin;
            leds[leds_cnt].on_high = entry_pin_on_high(tail);

            led_init(&leds[leds_cnt]);

            network_indicator.leds[0]           = &leds[leds_cnt];
            network_indicator.leds[1]           = NULL;
            network_indicator.has_dedicated_led = true;

            has_dedicated_status_led = true;
            leds_cnt++;
        } else if (entry[0] == 'I') {
            const char *   tail = NULL;
            hal_gpio_pin_t pin  = parse_entry_pin(entry + 1, &tail);
            hal_gpio_init(pin, 0, HAL_GPIO_PULL_NONE);
            leds[leds_cnt].pin     = pin;
            leds[leds_cnt].on_high = entry_pin_on_high(tail);
            led_init(&leds[leds_cnt]);

            for (int index = 0; index < 4; index++) {
                if (relay_clusters[index].indicator_led == NULL) {
                    relay_clusters[index].indicator_led = &leds[leds_cnt];
                    break;
                }
            }

            for (int index = 0; index < 4; index++) {
                if (switch_clusters[index].indicator_led == NULL) {
                    switch_clusters[index].indicator_led = &leds[leds_cnt];
                    break;
                }
            }

            if (!has_dedicated_status_led) {
                for (int index = 0; index < 4; index++) {
                    if (network_indicator.leds[index] == NULL) {
                        network_indicator.leds[index] = &leds[leds_cnt];
                        break;
                    }
                }
            }
            leds_cnt++;
        } else if (entry[0] == 'S') {
            const char *    tail = NULL;
            hal_gpio_pin_t  pin  = parse_entry_pin(entry + 1, &tail);
            hal_gpio_pull_t pull = parse_entry_pull(tail);
            hal_gpio_init(pin, 1, pull);

            buttons[buttons_cnt].pin = pin;
            buttons[buttons_cnt].long_press_duration_ms  = 800;
            buttons[buttons_cnt].multi_press_duration_ms = 800;
            buttons[buttons_cnt].debounce_delay_ms       = debounce_ms;
            buttons[buttons_cnt].on_multi_press          = on_multi_press_reset;

            buttons[buttons_cnt].pressed_when_high =
                entry_pressed_when_high(pull);
            switch_clusters[switch_clusters_cnt].switch_idx = switch_clusters_cnt;
            switch_clusters[switch_clusters_cnt].mode       =
                ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE;
            switch_clusters[switch_clusters_cnt].action =
                ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SIMPLE;
            switch_clusters[switch_clusters_cnt].relay_mode =
                ZCL_ONOFF_CONFIGURATION_RELAY_MODE_SHORT;
            switch_clusters[switch_clusters_cnt].binded_mode =
                ZCL_ONOFF_CONFIGURATION_BINDED_MODE_SHORT;
            switch_clusters[switch_clusters_cnt].relay_index     = switch_clusters_cnt + 1;
            switch_clusters[switch_clusters_cnt].button          = &buttons[buttons_cnt];
            switch_clusters[switch_clusters_cnt].level_move_rate = 50;
            buttons_cnt++;
            switch_clusters_cnt++;
        } else if (entry[0] == 'R') {
            const char *   tail = NULL;
            hal_gpio_pin_t pin  = parse_entry_pin(entry + 1, &tail);
            hal_gpio_init(pin, 0, HAL_GPIO_PULL_NONE);

            relays[relays_cnt].pin     = pin;
            relays[relays_cnt].on_high = 1;

            if (tail != NULL && tail[0] >= 'A' && tail[0] <= 'Z') {
                pin = parse_entry_pin(tail, NULL);
                hal_gpio_init(pin, 0, HAL_GPIO_PULL_NONE);
                relays[relays_cnt].off_pin     = pin;
                relays[relays_cnt].is_latching = 1;
            }

            relay_clusters[relay_clusters_cnt].relay_idx = relay_clusters_cnt;
            relay_clusters[relay_clusters_cnt].relay     = &relays[relays_cnt];

            relays_cnt++;
            relay_clusters_cnt++;
        } else if (entry[0] == 'X') {
            const char *    tail      = NULL;
            hal_gpio_pin_t  open_pin  = parse_entry_pin(entry + 1, &tail);
            hal_gpio_pin_t  close_pin = parse_entry_pin(tail, &tail);
            hal_gpio_pull_t pull      = parse_entry_pull(tail);

            hal_gpio_init(open_pin, 1, pull);
            hal_gpio_init(close_pin, 1, pull);

            buttons[buttons_cnt].pin = open_pin;
            buttons[buttons_cnt].long_press_duration_ms  = 800;
            buttons[buttons_cnt].multi_press_duration_ms = 800;
            buttons[buttons_cnt].debounce_delay_ms       = debounce_ms;
            buttons[buttons_cnt].pressed_when_high       =
                entry_pressed_when_high(pull);
            buttons[buttons_cnt].on_multi_press = on_multi_press_reset;
            button_t *open_button = &buttons[buttons_cnt++];

            buttons[buttons_cnt].pin = close_pin;
            buttons[buttons_cnt].long_press_duration_ms  = 800;
            buttons[buttons_cnt].multi_press_duration_ms = 800;
            buttons[buttons_cnt].debounce_delay_ms       = debounce_ms;
            buttons[buttons_cnt].pressed_when_high       =
                entry_pressed_when_high(pull);
            buttons[buttons_cnt].on_multi_press = on_multi_press_reset;
            button_t *close_button = &buttons[buttons_cnt++];

            cover_switch_clusters[cover_switch_clusters_cnt].open_button =
                open_button;
            cover_switch_clusters[cover_switch_clusters_cnt].close_button =
                close_button;
            cover_switch_clusters[cover_switch_clusters_cnt].cover_switch_idx =
                cover_switch_clusters_cnt;
            cover_switch_clusters_cnt++;
        } else if (entry[0] == 'C') {
            const char *   tail      = NULL;
            hal_gpio_pin_t open_pin  = parse_entry_pin(entry + 1, &tail);
            hal_gpio_pin_t close_pin = parse_entry_pin(tail, NULL);

            hal_gpio_init(open_pin, 0, HAL_GPIO_PULL_NONE);
            hal_gpio_init(close_pin, 0, HAL_GPIO_PULL_NONE);

            relays[relays_cnt].pin         = open_pin;
            relays[relays_cnt].on_high     = 1;
            relays[relays_cnt].is_latching = 0;
            relay_t *open_relay = &relays[relays_cnt++];

            relays[relays_cnt].pin         = close_pin;
            relays[relays_cnt].on_high     = 1;
            relays[relays_cnt].is_latching = 0;
            relay_t *close_relay = &relays[relays_cnt++];

            cover_clusters[cover_clusters_cnt].open_relay  = open_relay;
            cover_clusters[cover_clusters_cnt].close_relay = close_relay;
            cover_clusters[cover_clusters_cnt].cover_idx   = cover_clusters_cnt;
            cover_clusters_cnt++;
        } else if (entry[0] == 'i') {
            uint32_t image_type = parse_int(entry + 1);
            hal_zigbee_set_image_type(image_type);
        } else if (entry[0] == 'M') {
            for (int index = 0; index < switch_clusters_cnt; index++) {
                switch_clusters[index].mode =
                    ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY;
            }
        }
    }

    peripherals_init();

    printf("Initializing Zigbee with %d switches, %d relays, %d cover switches, "
           "%d covers\r\n",
           switch_clusters_cnt, relay_clusters_cnt, cover_switch_clusters_cnt,
           cover_clusters_cnt);

    uint8_t total_endpoints = switch_clusters_cnt + relay_clusters_cnt +
                              cover_switch_clusters_cnt + cover_clusters_cnt;

    hal_zigbee_cluster *cluster_ptr = clusters;

    for (int index = 0; index < switch_clusters_cnt; index++) {
        if (switch_clusters[index].relay_index > relay_clusters_cnt) {
            // Detach switches that point past the available relay count.
            switch_clusters[index].relay_mode =
                ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED;
            switch_clusters[index].relay_index = 0;
        }
    }

    // special case when no switches or relays are defined, so we can init a
    // "clean" device and configure it while running endpoint 1 still needs to be
    // initialised even though wenn no switches or relays are defined, so it can
    // join the network!
    if (total_endpoints == 0)
        total_endpoints = 1;

    for (int index = 0; index < total_endpoints; index++) {
        endpoints[index].endpoint   = index + 1;
        endpoints[index].profile_id = 0x0104;
        endpoints[index].device_id  = 0xffff;
    }

    endpoints[0].clusters = cluster_ptr;
    basic_cluster_add_to_endpoint(&basic_cluster, &endpoints[0]);

    hal_ota_cluster_setup(&endpoints[0].clusters[endpoints[0].cluster_count]);
    endpoints[0].cluster_count++;

    // Add battery cluster for battery-powered devices
    if (battery.pin != HAL_INVALID_PIN) {
        static zigbee_battery_cluster battery_cluster;
        battery_cluster_add_to_endpoint(&battery_cluster, &endpoints[0]);
    }

#ifdef END_DEVICE
    // Add poll control cluster for end devices
    static zigbee_poll_control_cluster poll_ctrl_cluster;
    poll_control_cluster_add_to_endpoint(&poll_ctrl_cluster, &endpoints[0],
                                         battery.pin != HAL_INVALID_PIN);
#endif

    for (int index = 0; index < switch_clusters_cnt; index++) {
        if (index != 0) {
            cluster_ptr += endpoints[index - 1].cluster_count;
            endpoints[index].clusters = cluster_ptr;
        }
        switch_cluster_add_to_endpoint(&switch_clusters[index], &endpoints[index]);
    }
    for (int index = 0; index < relay_clusters_cnt; index++) {
        if (switch_clusters_cnt + index != 0) {
            cluster_ptr += endpoints[switch_clusters_cnt + index - 1].cluster_count;
            endpoints[switch_clusters_cnt + index].clusters = cluster_ptr;
        }
        relay_cluster_add_to_endpoint(&relay_clusters[index],
                                      &endpoints[switch_clusters_cnt + index]);
        // Group cluster is stateless, safe to add to multiple endpoints
        group_cluster_add_to_endpoint(&group_cluster,
                                      &endpoints[switch_clusters_cnt + index]);
    }

    int cover_switch_base = switch_clusters_cnt + relay_clusters_cnt;
    for (int index = 0; index < cover_switch_clusters_cnt; index++) {
        if (cover_switch_base + index != 0) {
            cluster_ptr += endpoints[cover_switch_base + index - 1].cluster_count;
            endpoints[cover_switch_base + index].clusters = cluster_ptr;
        }
        cover_switch_cluster_add_to_endpoint(&cover_switch_clusters[index],
                                             &endpoints[cover_switch_base + index]);
    }

    int cover_base =
        switch_clusters_cnt + relay_clusters_cnt + cover_switch_clusters_cnt;
    for (int index = 0; index < cover_clusters_cnt; index++) {
        if (cover_base + index != 0) {
            cluster_ptr += endpoints[cover_base + index - 1].cluster_count;
            endpoints[cover_base + index].clusters = cluster_ptr;
        }
        cover_cluster_add_to_endpoint(&cover_clusters[index],
                                      &endpoints[cover_base + index]);
    }

    hal_zigbee_init(endpoints, total_endpoints);

    printf("Config parsed successfully\r\n");
}

void network_indicator_on_network_status_change(
    hal_zigbee_network_status_t new_status) {
    printf("Network status changed to %d\r\n", new_status);
    if (new_status == HAL_ZIGBEE_NETWORK_JOINED) {
        if (battery.pin != HAL_INVALID_PIN) {
            network_indicator.manual_state_when_connected = 0;
        }
        network_indicator_connected(&network_indicator);
        update_switch_clusters();
        update_relay_clusters();
    } else {
        network_indicator_not_connected(&network_indicator);
    }
}

void peripherals_init() {
    for (int index = 0; index < buttons_cnt; index++) {
        btn_init(&buttons[index]);
    }
    for (int index = 0; index < leds_cnt; index++) {
        led_init(&leds[index]);
    }
    for (int index = 0; index < relays_cnt; index++) {
        relay_init(&relays[index]);
    }
    if (hal_zigbee_get_network_status() == HAL_ZIGBEE_NETWORK_JOINED) {
        network_indicator_connected(&network_indicator);
        update_switch_clusters();
        update_relay_clusters();
    } else {
        network_indicator_not_connected(&network_indicator);
    }
    hal_register_on_network_status_change_callback(
        network_indicator_on_network_status_change);
}

// Helper functions

char *seek_until(char *cursor, char needle) {
    while (*cursor != needle && *cursor != '\0') {
        cursor++;
    }
    return(cursor);
}

char *extract_next_entry(char **cursor) {
    char *end = seek_until(*cursor, ';');

    *end = '\0';
    char *res = *cursor;
    *cursor = end + 1;
    return(res);
}

uint32_t parse_int(const char *s) {
    if (!s)
        return 0;

    uint32_t n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return n;
}

static bool device_config_copy_parse_buffer(char *dst, size_t dst_size) {
    if (device_config_str.size == 0 ||
        device_config_str.size > sizeof(device_config_str.data)) {
        printf("Invalid device config length: %u\r\n", device_config_str.size);
        return false;
    }

    if ((size_t)device_config_str.size + 1 > dst_size) {
        printf("Device config buffer too small for parsing\r\n");
        return false;
    }

    if (memchr(device_config_str.data, '\0', device_config_str.size) != NULL) {
        printf("Invalid device config: embedded NUL byte\r\n");
        return false;
    }

    memcpy(dst, device_config_str.data, device_config_str.size);
    dst[device_config_str.size] = '\0';

    if (dst[device_config_str.size - 1] != ';') {
        printf("Invalid device config: missing trailing ';'\r\n");
        return false;
    }

    return true;
}

static bool device_config_validate_current(device_config_usage_t *usage) {
    char parse_buf[sizeof(device_config_str.data) + 1];

    return device_config_copy_parse_buffer(parse_buf, sizeof(parse_buf)) &&
           device_config_validate_buffer(parse_buf, usage);
}

static bool device_config_validate_buffer(char *buffer,
                                          device_config_usage_t *usage) {
    device_config_usage_t local_usage = { 0 };
    char *  cursor = buffer;
    size_t  cluster_count;
    uint8_t endpoint_count;

    const char *zb_manufacturer = extract_next_entry(&cursor);
    const char *zb_model        = extract_next_entry(&cursor);

    if (strlen(zb_manufacturer) > 31) {
        printf("Manufacturer too big\r\n");
        return false;
    }

    if (strlen(zb_model) > 31) {
        printf("Model too big\r\n");
        return false;
    }

    for (char *entry = extract_next_entry(&cursor); *entry != '\0';
         entry = extract_next_entry(&cursor)) {
        size_t len = strlen(entry);

        if (strcmp(entry, "SLP") == 0) {
            continue;
        }

        switch (entry[0]) {
        case 'D':
        case 'i':
            if (len < 2) {
                printf("Invalid config entry: %s\r\n", entry);
                return false;
            }
            break;

        case 'M':
            if (len != 1) {
                printf("Invalid config entry: %s\r\n", entry);
                return false;
            }
            break;

        case 'B':
            if (entry[1] == 'T') {
                if (len < 4) {
                    printf("Invalid battery config entry: %s\r\n", entry);
                    return false;
                }
                local_usage.has_battery = true;
                break;
            }
            if (len < 3) {
                printf("Invalid button config entry: %s\r\n", entry);
                return false;
            }
            local_usage.buttons++;
            break;

        case 'L':
        case 'I':
            if (len < 2) {
                printf("Invalid LED config entry: %s\r\n", entry);
                return false;
            }
            local_usage.leds++;
            break;

        case 'S':
            if (len < 3) {
                printf("Invalid switch config entry: %s\r\n", entry);
                return false;
            }
            local_usage.buttons++;
            local_usage.switch_endpoints++;
            break;

        case 'R':
            if (len < 2) {
                printf("Invalid relay config entry: %s\r\n", entry);
                return false;
            }
            local_usage.relay_outputs++;
            local_usage.relay_endpoints++;
            break;

        case 'X':
            if (len < 5) {
                printf("Invalid cover switch config entry: %s\r\n", entry);
                return false;
            }
            local_usage.buttons += 2;
            local_usage.cover_switch_endpoints++;
            break;

        case 'C':
            if (len < 4) {
                printf("Invalid cover config entry: %s\r\n", entry);
                return false;
            }
            local_usage.relay_outputs += 2;
            local_usage.cover_endpoints++;
            break;

        default:
            printf("Unknown config entry: %s\r\n", entry);
            return false;
        }
    }

    if (local_usage.leds > ARRAY_LEN(leds) ||
        local_usage.buttons > ARRAY_LEN(buttons) ||
        local_usage.relay_outputs > ARRAY_LEN(relays) ||
        local_usage.switch_endpoints > ARRAY_LEN(switch_clusters) ||
        local_usage.relay_endpoints > ARRAY_LEN(relay_clusters) ||
        local_usage.cover_switch_endpoints > ARRAY_LEN(cover_switch_clusters) ||
        local_usage.cover_endpoints > ARRAY_LEN(cover_clusters)) {
        printf("Device config exceeds static resource limits\r\n");
        return false;
    }

    endpoint_count = local_usage.switch_endpoints +
                     local_usage.relay_endpoints +
                     local_usage.cover_switch_endpoints +
                     local_usage.cover_endpoints;
    if (endpoint_count == 0) {
        endpoint_count = 1;
    }

    if (endpoint_count > ARRAY_LEN(endpoints)) {
        printf("Device config exceeds endpoint capacity\r\n");
        return false;
    }

    cluster_count = 2 +
                    4 * local_usage.switch_endpoints +
                    3 * local_usage.relay_endpoints +
                    3 * local_usage.cover_switch_endpoints +
                    local_usage.cover_endpoints;
    if (local_usage.has_battery) {
        cluster_count++;
    }
#ifdef END_DEVICE
    cluster_count++;
#endif

    if (cluster_count > ARRAY_LEN(clusters)) {
        printf("Device config exceeds cluster capacity\r\n");
        return false;
    }

    if (usage != NULL) {
        *usage = local_usage;
    }
    return true;
}
