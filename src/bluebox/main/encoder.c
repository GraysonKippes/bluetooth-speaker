#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "encoder.h"
#include "common.h"

// Defines/Macros //

#define PIN_BITMASK(x) (1lu << x)

// Type Definitions //

typedef enum {
    ENCODER_TERMINAL_A = 0,
    ENCODER_TERMINAL_B = 1
} EncoderTerminal_t;

typedef struct {
    EncoderTerminal_t terminal;
} EncoderPayload_t;

// Global Variables //

static bool initialized = false;

static int last_level_a = 0; // Last recorded level on the terminal A GPIO pin.
static int last_level_b = 0; // Last recorded level on the terminal B GPIO pin.

// How many rotation ticks are needed to register a rotation input.
static uint32_t rotation_speed = 1;

static uint32_t gpio_terminal_a = 0;
static uint32_t gpio_terminal_b = 0;

static TaskHandle_t task_handle_encoder_rotate = NULL;
static QueueHandle_t encoder_rotate_queue = NULL;

static EncoderCallback_t encoder_callback = NULL;

// Private Function Definitions //

// Handles rotation inputs from the encoder.
static void task_encoder_rotate(void *param)
{
    static uint32_t cw_counter = 0; // Clockwise tick counter
    static uint32_t ccw_counter = 0; // Counter-clockwise tick counter

    EncoderPayload_t payload = {};
    while (true) {
        if (xQueueReceive(encoder_rotate_queue, &payload, portMAX_DELAY)) {

            // Handle encoder rotate event.
            // Use the quadrature to determine which way the knob is being turned.
            int level = 0;
            if (payload.terminal == ENCODER_TERMINAL_A) {
                level = gpio_get_level(gpio_terminal_a);
                if (level != last_level_a) {
                    last_level_a = level;
                    if (last_level_a == 1 && last_level_b == 1) {
                        ++ccw_counter;
                        if (cw_counter > 0) --cw_counter;
                    }
                }
            } else if (payload.terminal == ENCODER_TERMINAL_B) {
                level = gpio_get_level(gpio_terminal_b);
                if (level != last_level_b) {
                    last_level_b = level;
                    if (last_level_a == 1 && last_level_b == 1) {
                        ++cw_counter;
                        if (ccw_counter > 0) --ccw_counter;
                    }
                }
            }

            // Handle rotation ticking logic.
            if (cw_counter >= rotation_speed) {
                cw_counter -= rotation_speed;
                if (encoder_callback) encoder_callback(ROTATION_DIRECTION_CLOCKWISE); 
            }
            if (ccw_counter >= rotation_speed) {
                ccw_counter -= rotation_speed;
                if (encoder_callback) encoder_callback(ROTATION_DIRECTION_COUNTERCLOCKWISE); 
            }
        }
    }
}

static void IRAM_ATTR enc_a_isr_handler(void *param)
{
    const EncoderPayload_t payload = {
        .terminal = ENCODER_TERMINAL_A
    };
    xQueueSendFromISR(encoder_rotate_queue, &payload, NULL);
}

static void IRAM_ATTR enc_b_isr_handler(void *param)
{
    const EncoderPayload_t payload = {
        .terminal = ENCODER_TERMINAL_B
    };
    xQueueSendFromISR(encoder_rotate_queue, &payload, NULL);
}

// Public Function Definitions //

int init_encoder(uint32_t encoder_a, uint32_t encoder_b)
{
    esp_err_t ret = ESP_OK;

    // Rotary encoder GPIO config
    const gpio_config_t config1 = {
        .pin_bit_mask = PIN_BITMASK(encoder_a) | PIN_BITMASK(encoder_b),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    ret = gpio_config(&config1);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "failed to initialize encoder rotation GPIO pins: invalid argument.");
        return -1;
    }

    // Initialize terminal level trackers.
    last_level_a = gpio_get_level(encoder_a);
    last_level_b = gpio_get_level(encoder_b);

    // Create queue/tasks.
    encoder_rotate_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(task_encoder_rotate, "tasK_encoder_rotate", 2048, NULL, 10, &task_handle_encoder_rotate);

    // Install interrupt service and handlers.
    gpio_install_isr_service(0);
    gpio_isr_handler_add(encoder_a, enc_a_isr_handler, (void *)encoder_a);
    gpio_isr_handler_add(encoder_b, enc_b_isr_handler, (void *)encoder_b);

    gpio_terminal_a = encoder_a;
    gpio_terminal_b = encoder_b;

    initialized = true;

    return 0;
}

int get_gpio_terminal_a(uint32_t *const pOut)
{
    if (!pOut) {
        ESP_LOGE(LOG_TAG, "failed to get GPIO pin for encoder terminal A: pOut is NULL.");
        return -1;
    }
    if (initialized) {
        ESP_LOGE(LOG_TAG, "failed to get GPIO pin for encoder terminal A: encoder is not initialized.");
        return 1;
    }
    *pOut = gpio_terminal_a;
    return 0;
}

int get_gpio_terminal_b(uint32_t *const pOut)
{
    if (!pOut) {
        ESP_LOGE(LOG_TAG, "failed to get GPIO pin for encoder terminal B: pOut is NULL.");
        return -1;
    }
    if (initialized) {
        ESP_LOGE(LOG_TAG, "failed to get GPIO pin for encoder terminal B: encoder is not initialized.");
        return 1;
    }
    *pOut = gpio_terminal_b;
    return 0;
}

// Set the number of detents needed to register a rotation input.
int set_detents_per_input(uint32_t x)
{
    if (x > 0) {
        rotation_speed = x;
        return 0;
    }
    ESP_LOGE(LOG_TAG, "failed to set detents-per-input: invalid value (%lu).", x);
    return -1;
}

int set_encoder_callback(EncoderCallback_t callback)
{
    encoder_callback = callback;
    return 0;
}