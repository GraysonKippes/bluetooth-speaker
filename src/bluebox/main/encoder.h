#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

// Type Definitions //

typedef enum {
    ROTATION_DIRECTION_CLOCKWISE = 0,
    ROTATION_DIRECTION_COUNTERCLOCKWISE = 1
} RotationDirection_t;

// A function called when the rotary encoder is rotated.
typedef void (*EncoderCallback_t)(RotationDirection_t direction);

// Function Declarations //

// Initializes everything needed to read input from a rotary encoder (without a built-in switch).
// encoder_a = GPIO pin that connects to encoder terminal A.
// encoder_b = GPIO pin that connects to encoder terminal B.
int init_encoder(uint32_t encoder_a, uint32_t encoder_b);

// Initializes everything needed to read input from a rotary encoder with a built-in switch.
// encoder_a = GPIO pin that connects to encoder terminal A.
// encoder_b = GPIO pin that connects to encoder terminal B.
// encoder_sw = GPIO pin that connects to encoder pushbutton switch terminal.
//int init_encoder_switch(uint32_t encoder_a, uint32_t encoder_b, uint32_t encoder_sw);

// Gets the GPIO pin connected to encoder terminal A.
// The encoder must be initialized first.
int get_gpio_terminal_a(uint32_t *const pOut);

// Gets the GPIO pin connected to encoder terminal B.
// The encoder must be initialized first.
int get_gpio_terminal_b(uint32_t *const pOut);

int set_detents_per_input(uint32_t x);

int set_encoder_callback(EncoderCallback_t callback);

#endif // ENCODER_H