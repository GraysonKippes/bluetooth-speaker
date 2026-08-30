#ifndef SWITCH_H
#define SWITCH_H

#include <stdint.h>

// TYPE DEFINITIONS //

// Represents a digital logic level, as reported by a GPIO pin.
typedef enum {
    LOGIC_LEVEL_LOW = 0,
    LOGIC_LEVEL_HIGH = 1
} LogicLevel_t;

// Specifies what kinds of edge on which to trigger an interrupt for a particular GPIO pin.
typedef enum {
    INTERRUPT_TYPE_RISING_EDGE,
    INTERRUPT_TYPE_FALLING_EDGE,
    INTERRUPT_TYPE_ANY_EDGE
} InterruptType_t;

// A function called when a switch interrupt is activated.
typedef void (*SwitchCallback_t)(uint32_t pin, LogicLevel_t level);

// PUBLIC FUNCTION DEFINITIONS //

int add_switch_callback(uint32_t pin, InterruptType_t interrupt_type, SwitchCallback_t callback);

#endif // SWITCH_H