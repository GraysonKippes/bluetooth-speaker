#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "switch.h"
#include "common.h"

// DEFINITIONS //

#define MAX_GPIO 40 // Highest-numbered GPIO pin, plus one.
#define PIN_BITMASK(x) (1lu << x)

// TYPE DEFINITIONS //

typedef struct {
    bool enabled;
    SwitchCallback_t callback;
} GPIO_t;

// PRIVATE GLOBAL VARIABLE DEFINITIONS //

static bool switch_service_inited = false;
static TaskHandle_t task_handle_switch_input = NULL;
static QueueHandle_t switch_input_queue = NULL;

static SwitchCallback_t callbacks[MAX_GPIO] = {};

// PRIVATE FUNCTION DEFINITIONS //

static void IRAM_ATTR switch_isr_handler(void *param)
{
    uint32_t pin = (uint32_t)param;
    xQueueSendFromISR(switch_input_queue, &pin, NULL);
}

static void task_switch_input(void *param)
{
    uint32_t pin = 0;
    while (true) {
        if (xQueueReceive(switch_input_queue, &pin, portMAX_DELAY)) {
            if (pin < MAX_GPIO) {
                if (callbacks[pin]) {
                    LogicLevel_t level = (LogicLevel_t)gpio_get_level(pin);
                    callbacks[pin](pin, level);
                }
            }
        }
    }
}

// PUBLIC FUNCTION DEFINITIONS //

int add_switch_callback(uint32_t pin, InterruptType_t interrupt_type, SwitchCallback_t callback)
{
    esp_err_t ret = ESP_OK;

    if (pin >= MAX_GPIO) {
        ESP_LOGE(LOG_TAG, "failed to add switch callback: GPIO pin number (%lu) too high.", pin);
        return -1;
    }

    gpio_config_t config = {
        .pin_bit_mask = PIN_BITMASK(pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    switch (interrupt_type) {
        case INTERRUPT_TYPE_RISING_EDGE:
            config.intr_type = GPIO_INTR_POSEDGE;
            break;
        case INTERRUPT_TYPE_FALLING_EDGE:
            config.intr_type = GPIO_INTR_NEGEDGE;
            break;
        case INTERRUPT_TYPE_ANY_EDGE:
            config.intr_type = GPIO_INTR_ANYEDGE;
            break;
    }

    ret = gpio_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "failed to add switch callback: invalid GPIO configuration.");
        return -1;
    }

    if (!switch_service_inited) {

        for (uint32_t i = 0; i < MAX_GPIO; ++i) {
            callbacks[i] = NULL;
        }

        switch_input_queue = xQueueCreate(10, sizeof(uint32_t));
        xTaskCreate(task_switch_input, "tasK_switch_input", 2048, NULL, 10, &task_handle_switch_input);

        gpio_install_isr_service(0);
        switch_service_inited = true;
    }
    gpio_isr_handler_add(pin, switch_isr_handler, (void *)pin);

    return 0;
}