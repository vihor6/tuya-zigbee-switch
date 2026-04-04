#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "zigbee_app_framework_event.h"

#include "hal/gpio.h"
#include "silabs/hal/silabs_gpio_utils.h"

// Get container structure from embedded member pointer
#define container_of(ptr, type, member) \
        ((type *)((char *)(ptr) - offsetof(type, member)))

#define LINE_MISSING     0xFF
#define MAX_INT_LINES    16

#if __has_include("sl_gpio.h")

#include "sl_clock_manager.h"
#include "sl_gpio.h"

// ------ One-time init guards ------
static bool s_gpio_clock_enabled = false;
static bool s_gpio_inited        = false;

static inline void hal_gpio_enable_clock(void) {
    (void)sl_clock_manager_enable_bus_clock(SL_BUS_CLOCK_GPIO);
}

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

static void _dispatch_regular(uint8_t intNo, void *ctx) {
    (void)intNo;
    int_slot_t *slot = (int_slot_t *)ctx;
    if (slot) {
        sl_zigbee_af_event_set_active(&slot->af_event);
    }
}

static void _af_event_handler(sl_zigbee_af_event_t *event) {
    int_slot_t *slot = container_of(event, int_slot_t, af_event);

    if (slot->user_cb) {
        slot->user_cb(slot->hal_pin, slot->arg);
    }
}

void hal_gpio_init(hal_gpio_pin_t gpio_pin, uint8_t is_input,
                   hal_gpio_pull_t pull_direction) {
    hal_gpio_ensure_clock();

    const sl_gpio_t sl_gpio = silabs_hal_gpio_to_sl_gpio(gpio_pin);

    if (is_input) {
        switch (pull_direction) {
        case HAL_GPIO_PULL_UP:
            sl_gpio_set_pin_mode(&sl_gpio, SL_GPIO_MODE_INPUT_PULL, 1);
            break;
        case HAL_GPIO_PULL_DOWN:
            sl_gpio_set_pin_mode(&sl_gpio, SL_GPIO_MODE_INPUT_PULL, 0);
            break;
        default:
            sl_gpio_set_pin_mode(&sl_gpio, SL_GPIO_MODE_INPUT, 0);
            break;
        }
    } else {
        sl_gpio_set_pin_mode(&sl_gpio, SL_GPIO_MODE_PUSH_PULL, 0);
    }
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

void hal_gpio_callback(hal_gpio_pin_t gpio_pin, gpio_callback_t callback,
                       void *arg) {
    hal_gpio_ensure_clock();
    hal_gpio_ensure_gpio_init();

    const sl_gpio_t sl_gpio = silabs_hal_gpio_to_sl_gpio(gpio_pin);

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

    sl_status_t status = sl_gpio_configure_external_interrupt(
        &sl_gpio, &slot->line, SL_GPIO_INTERRUPT_RISING_FALLING_EDGE,
        (sl_gpio_irq_callback_t)_dispatch_regular, slot);
    printf("hal_gpio_callback: exti line %ld status %lu\r\n", slot->line,
           (unsigned long)status);
}

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

#else

#include "em_cmu.h"
#include "em_device.h"
#include "em_gpio.h"
#include "gpiointerrupt.h"

// ------ One-time init guards ------
static bool s_gpio_clock_enabled = false;
static bool s_gpioint_inited     = false;

static void hal_gpio_ensure_clock(void) {
    if (!s_gpio_clock_enabled) {
        CMU_ClockEnable(cmuClock_GPIO, true);
        s_gpio_clock_enabled = true;
    }
}

static void hal_gpio_ensure_gpioint(void) {
    if (!s_gpioint_inited) {
        GPIOINT_Init();
        s_gpioint_inited = true;
    }
}

// ------ Per-interrupt bookkeeping ------
typedef struct {
    bool             in_use;
    uint8_t          em4wu_int_no;
    hal_gpio_pin_t   hal_pin;
    uint8_t          pull_dir;
    gpio_callback_t  user_cb;
    void *           arg;
    sl_zigbee_event_t af_event;
} int_slot_t;

static int_slot_t s_slots[MAX_INT_LINES];

static uint8_t alloc_int_line(void) {
    for (uint8_t i = 0; i < MAX_INT_LINES; i++) {
        if (!s_slots[i].in_use) {
            s_slots[i].in_use = true;
            return i;
        }
    }
    return LINE_MISSING;
}

static void free_int_line(uint8_t int_no) {
    if (int_no < MAX_INT_LINES) {
        memset(&s_slots[int_no], 0, sizeof(s_slots[int_no]));
    }
}

static void _dispatch_regular(uint8_t intNo, void *ctx) {
    (void)intNo;
    int_slot_t *slot = (int_slot_t *)ctx;
    if (slot) {
        sl_zigbee_event_set_active(&slot->af_event);
    }
}

static void _dispatch_em4wu(uint8_t intNo, void *ctx) {
    (void)intNo;
    int_slot_t *slot = (int_slot_t *)ctx;
    if (slot) {
        sl_zigbee_event_set_active(&slot->af_event);
    }
}

static void _af_event_handler(sl_zigbee_event_t *event) {
    int_slot_t *slot = (int_slot_t *)event->data;
    if (slot->user_cb) {
        slot->user_cb(slot->hal_pin, slot->arg);
    }
}

void hal_gpio_init(hal_gpio_pin_t gpio_pin, uint8_t is_input,
                   hal_gpio_pull_t pull_direction) {
    hal_gpio_ensure_clock();

    GPIO_Port_TypeDef port    = silabs_hal_gpio_port(gpio_pin);
    uint8_t           pin_num = silabs_hal_gpio_pin_number(gpio_pin);

    if (is_input) {
        switch (pull_direction) {
        case HAL_GPIO_PULL_UP:
            GPIO_PinModeSet(port, pin_num, gpioModeInputPull, 1);
            break;
        case HAL_GPIO_PULL_DOWN:
            GPIO_PinModeSet(port, pin_num, gpioModeInputPull, 0);
            break;
        default:
            GPIO_PinModeSet(port, pin_num, gpioModeInput, 0);
            break;
        }
    } else {
        GPIO_PinModeSet(port, pin_num, gpioModePushPull, 0);
    }
}

void hal_gpio_set(hal_gpio_pin_t gpio_pin) {
    GPIO_PinOutSet(silabs_hal_gpio_port(gpio_pin),
                   silabs_hal_gpio_pin_number(gpio_pin));
}

void hal_gpio_clear(hal_gpio_pin_t gpio_pin) {
    GPIO_PinOutClear(silabs_hal_gpio_port(gpio_pin),
                     silabs_hal_gpio_pin_number(gpio_pin));
}

uint8_t hal_gpio_read(hal_gpio_pin_t gpio_pin) {
    return GPIO_PinInGet(silabs_hal_gpio_port(gpio_pin),
                         silabs_hal_gpio_pin_number(gpio_pin));
}

void hal_gpio_callback(hal_gpio_pin_t gpio_pin, gpio_callback_t callback,
                       void *arg) {
    hal_gpio_ensure_clock();
    hal_gpio_ensure_gpioint();

    GPIO_Port_TypeDef port    = silabs_hal_gpio_port(gpio_pin);
    uint8_t           pin_num = silabs_hal_gpio_pin_number(gpio_pin);

    uint8_t line = alloc_int_line();
    if (line == LINE_MISSING) {
        printf("hal_gpio_callback: no free EXTI lines\r\n");
        return;
    }

    int_slot_t *slot = &s_slots[line];
    slot->hal_pin = gpio_pin;
    slot->user_cb = callback;
    slot->arg     = arg;
    sl_zigbee_af_isr_event_init(&slot->af_event, _af_event_handler);
    slot->af_event.data = (uint32_t)slot;

    GPIO_Mode_TypeDef mode = GPIO_PinModeGet(port, pin_num);
    if (mode == gpioModeInputPull) {
        slot->pull_dir = GPIO_PinOutGet(port, pin_num) ? HAL_GPIO_PULL_UP
                                                       : HAL_GPIO_PULL_DOWN;
    } else {
        slot->pull_dir = HAL_GPIO_PULL_NONE;
    }

    unsigned int reg_int = GPIOINT_CallbackRegisterExt(
        line, (GPIOINT_IrqCallbackPtrExt_t)_dispatch_regular, slot);
    if (reg_int == INTERRUPT_UNAVAILABLE) {
        free_int_line(line);
        printf("hal_gpio_callback: interrupt unavailable\r\n");
        return;
    }
    GPIO_ExtIntConfig(port, pin_num, line, true, true, true);

    // Gecko SDK 4.4.6 exposes regular GPIOINT callbacks here but the
    // EM4WU extended registration symbol is not linkable in this CI/tool
    // combination, so keep the regular EXTI path only for the MG13 build.
    slot->em4wu_int_no = LINE_MISSING;
}

void hal_gpio_unreg_callback(hal_gpio_pin_t gpio_pin) {
    GPIO_Port_TypeDef port    = silabs_hal_gpio_port(gpio_pin);
    uint8_t           pin_num = silabs_hal_gpio_pin_number(gpio_pin);

    uint8_t int_no       = LINE_MISSING;
    uint8_t em4wu_int_no = LINE_MISSING;
    for (uint8_t i = 0; i < MAX_INT_LINES; i++) {
        if (s_slots[i].in_use && s_slots[i].hal_pin == gpio_pin) {
            int_no       = i;
            em4wu_int_no = s_slots[i].em4wu_int_no;
            break;
        }
    }

    if (int_no != LINE_MISSING) {
        GPIO_ExtIntConfig(port, pin_num, int_no, false, false, false);
        GPIOINT_CallbackUnRegister(int_no);
        free_int_line(int_no);
    }
}

#endif

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
