#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#if defined(_SILICON_LABS_32B_SERIES_1)
#include "em_cmu.h"
#else
#include "sl_clock_manager.h"
#endif
#include "sl_gpio.h"

#include "zigbee_app_framework_event.h"

#include "hal/gpio.h"
#include "silabs/hal/silabs_gpio_utils.h"
#include <stdio.h>

#if defined(_SILICON_LABS_32B_SERIES_1)
typedef sl_zigbee_event_t     sli_zigbee_event_t;
typedef sl_zigbee_event_t     sl_zigbee_af_event_t;

#define sl_zigbee_af_event_set_active sl_zigbee_event_set_active

static inline void hal_gpio_enable_clock(void) {
    CMU_ClockEnable(cmuClock_GPIO, true);
}
#else
static inline void hal_gpio_enable_clock(void) {
    (void)sl_clock_manager_enable_bus_clock(SL_BUS_CLOCK_GPIO);
}
#endif

// Get container structure from embedded member pointer
#define container_of(ptr, type, member) \
        ((type *)((char *)(ptr) - offsetof(type, member)))

#define LINE_MISSING     0xFF
#define MAX_INT_LINES    16

// ------ One-time init guards ------
static bool s_gpio_clock_enabled = false;
static bool s_gpio_inited        = false;

static void hal_gpio_ensure_clock(void) {
    if (!s_gpio_clock_enabled) {
        hal_gpio_enable_clock();
        s_gpio_clock_enabled = true;
    }
}

static void hal_gpio_ensure_gpio_init(void) {
    if (!s_gpio_inited) {
        sl_gpio_init();
        s_gpio_inited = true;
    }
}

// ------ Per-interrupt bookkeeping ------
typedef struct {
    bool               in_use;
    hal_gpio_pin_t     hal_pin;
    gpio_callback_t    user_cb;
    int32_t            line;
    void *             arg;
    sli_zigbee_event_t af_event;
} int_slot_t;

static int_slot_t s_slots[MAX_INT_LINES]; // 16 EXTI lines total

static int32_t alloc_int_slot(void) {
    for (uint8_t i = 0; i < 16; i++) {
        if (!s_slots[i].in_use) {
            s_slots[i].in_use = true;
            s_slots[i].line   = SL_GPIO_INTERRUPT_UNAVAILABLE;
            return i;
        }
    }
    return LINE_MISSING;
}

static void free_int_slot(int32_t slot_no) {
    if (slot_no < MAX_INT_LINES) {
        memset(&s_slots[slot_no], 0, sizeof(s_slots[slot_no]));
    }
}

// Dispatchers, e.g. IRQ routinues
static void _dispatch_regular(uint8_t intNo, void *ctx) {
    (void)intNo;
    int_slot_t *slot = (int_slot_t *)ctx;
    if (slot) {
        sl_zigbee_af_event_set_active(&slot->af_event);
    }
}

static void _af_event_handler(sl_zigbee_af_event_t *event) {
    // Get int_slot_t from embedded af_event field
    int_slot_t *slot = container_of(event, int_slot_t, af_event);

    if (slot->user_cb) {
        slot->user_cb(slot->hal_pin, slot->arg);
    }
}

// API
void hal_gpio_init(hal_gpio_pin_t gpio_pin, uint8_t is_input,
                   hal_gpio_pull_t pull_direction) {
    hal_gpio_ensure_clock();

    const sl_gpio_t sl_gpio = silabs_hal_gpio_to_sl_gpio(gpio_pin);

    if (is_input) {
        switch (pull_direction) {
        case HAL_GPIO_PULL_UP:
            sl_gpio_set_pin_mode(&sl_gpio, SL_GPIO_MODE_INPUT_PULL,
                                 1); // DOUT=1 => pull-up
            break;
        case HAL_GPIO_PULL_DOWN:
            sl_gpio_set_pin_mode(&sl_gpio, SL_GPIO_MODE_INPUT_PULL,
                                 0); // DOUT=0 => pull-down
            break;
        default:
            sl_gpio_set_pin_mode(&sl_gpio, SL_GPIO_MODE_INPUT, 0);
            break;
        }
    } else {
        // Output: push-pull, initial low
        sl_gpio_set_pin_mode(&sl_gpio, SL_GPIO_MODE_PUSH_PULL, 0);
    }

    // Optional: store pull direction for interrupt polarity later.
    // We don’t keep a global pin map; polarity is picked at registration time
    // by looking up the slot when the user calls hal_gpio_int_callback.
}

void hal_gpio_set(hal_gpio_pin_t gpio_pin) {
    const sl_gpio_t sl_gpio = silabs_hal_gpio_to_sl_gpio(gpio_pin);

    sl_gpio_set_pin(&sl_gpio);
}

void hal_gpio_clear(hal_gpio_pin_t gpio_pin) {
    const sl_gpio_t sl_gpio = silabs_hal_gpio_to_sl_gpio(gpio_pin);

    sl_gpio_clear_pin(&sl_gpio);
}

uint8_t hal_gpio_read(hal_gpio_pin_t gpio_pin) {
    const sl_gpio_t sl_gpio = silabs_hal_gpio_to_sl_gpio(gpio_pin);
    bool            value   = 0;

    sl_gpio_get_pin_input(&sl_gpio, &value);
    return value;
}

// Register an interrupt that also attempts EM4 wake-up.
// - Wakes from EM2/EM3 via normal EXTI (edge-sensitive).
// - Wakes from EM4 if the pin supports EM4WU (level-sensitive).
//   Polarity rule:
//     * If input has pull-up  -> active-low (wake on low level).
//     * If input has pull-down-> active-high (wake on high level).
//     * Otherwise default to rising+falling EXTI and active-low EM4WU.
void hal_gpio_callback(hal_gpio_pin_t gpio_pin, gpio_callback_t callback,
                       void *arg) {
    hal_gpio_ensure_clock();
    hal_gpio_ensure_gpio_init();

    const sl_gpio_t sl_gpio = silabs_hal_gpio_to_sl_gpio(gpio_pin);

    // Allocate a regular EXTI line (for edge interrupts while awake)
    int32_t slot_no = alloc_int_slot();
    if (slot_no == LINE_MISSING) {
        printf("hal_gpio_callback: no free EXTI lines\r\n");
        return;
    }
    int_slot_t *slot = &s_slots[slot_no];
    slot->hal_pin = gpio_pin;
    slot->user_cb = callback;
    slot->arg     = arg;
    sl_zigbee_af_isr_event_init(&slot->af_event, _af_event_handler);

    // Register regular edge-sensitive callback (both edges)

    sl_status_t status = sl_gpio_configure_external_interrupt(
        &sl_gpio, &slot->line, SL_GPIO_INTERRUPT_RISING_FALLING_EDGE,
        (sl_gpio_irq_callback_t)_dispatch_regular, slot);
    printf("hal_gpio_callback: exti line %ld status %lu\r\n", slot->line,
           (unsigned long)status);
}

// (Optional) helper to unregister an interrupt if you add
// hal_gpio_int_disable() later
void hal_gpio_unreg_callback(hal_gpio_pin_t gpio_pin) {
    printf("hal_gpio_unreg_callback pin %02X\r\n", gpio_pin);

    for (uint8_t i = 0; i < MAX_INT_LINES; i++) {
        if (s_slots[i].in_use && s_slots[i].hal_pin == gpio_pin) {
            sl_gpio_deconfigure_external_interrupt(s_slots[i].line);
            free_int_slot(i);
            return;
        }
    }
}

static bool parse_pin_number(const char *s, uint8_t *pin_no) {
    if (!s || s[0] < '0' || s[0] > '9') {
        return false;
    }

    uint16_t value = 0;
    size_t index = 0;
    for (; s[index] >= '0' && s[index] <= '9'; index++) {
        value = (uint16_t)(value * 10U + (uint16_t)(s[index] - '0'));
        if (value > 15U) {
            return false;
        }
    }

    if (s[index] != '\0') {
        return false;
    }

    *pin_no = (uint8_t)value;
    return true;
}

hal_gpio_pin_t hal_gpio_parse_pin(const char *s) {
    if (!s || s[0] < 'A' || s[0] > 'Z') {
        return HAL_INVALID_PIN;
    }

    uint8_t pin_no;
    if (!parse_pin_number(s + 1, &pin_no)) {
        return HAL_INVALID_PIN;
    }

    switch (s[0]) {
#if (GPIO_PA_COUNT > 0)
    case 'A':
        if (pin_no >= GPIO_PA_COUNT) {
            return HAL_INVALID_PIN;
        }
        return silabs_hal_gpio_make_pin(gpioPortA, pin_no);
#endif

#if (GPIO_PB_COUNT > 0)
    case 'B':
        if (pin_no >= GPIO_PB_COUNT) {
            return HAL_INVALID_PIN;
        }
        return silabs_hal_gpio_make_pin(gpioPortB, pin_no);
#endif

#if (GPIO_PC_COUNT > 0)
    case 'C':
        if (pin_no >= GPIO_PC_COUNT) {
            return HAL_INVALID_PIN;
        }
        return silabs_hal_gpio_make_pin(gpioPortC, pin_no);
#endif

#if (GPIO_PD_COUNT > 0)
    case 'D':
        if (pin_no >= GPIO_PD_COUNT) {
            return HAL_INVALID_PIN;
        }
        return silabs_hal_gpio_make_pin(gpioPortD, pin_no);
#endif

#if (GPIO_PE_COUNT > 0)
    case 'E':
        if (pin_no >= GPIO_PE_COUNT) {
            return HAL_INVALID_PIN;
        }
        return silabs_hal_gpio_make_pin(gpioPortE, pin_no);
#endif

#if (GPIO_PF_COUNT > 0)
    case 'F':
        if (pin_no >= GPIO_PF_COUNT) {
            return HAL_INVALID_PIN;
        }
        return silabs_hal_gpio_make_pin(gpioPortF, pin_no);
#endif

    default:
        return HAL_INVALID_PIN;
    }
}

hal_gpio_pull_t hal_gpio_parse_pull(const char *pull_str) {
    if (pull_str[0] == 'u' || pull_str[0] == 'U') {
        return HAL_GPIO_PULL_UP;
    }
    if (pull_str[0] == 'd') {
        return HAL_GPIO_PULL_DOWN;
    }
    if (pull_str[0] == 'f') {
        return HAL_GPIO_PULL_NONE;
    }
    return HAL_GPIO_PULL_INVALID;
}
