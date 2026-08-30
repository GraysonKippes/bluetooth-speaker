#include <stdio.h>
#include "bluetooth.h"
#include "encoder.h"
#include "switch.h"

void sw_cb_bluetooth(uint32_t pin, LogicLevel_t level)
{

}

void sw_cb_pause_play(uint32_t pin, LogicLevel_t level)
{

}

void sw_cb_prev_track(uint32_t pin, LogicLevel_t level)
{

}

void sw_cb_next_track(uint32_t pin, LogicLevel_t level)
{

}

void sw_cb_mute(uint32_t pin, LogicLevel_t level)
{

}

void enc_cb_volume(RotationDirection_t direction)
{
    if (direction == ROTATION_DIRECTION_CLOCKWISE) {
        increase_volume();
    } else if (direction == ROTATION_DIRECTION_COUNTERCLOCKWISE) {
        decrease_volume();
    }
}

void app_main(void)
{
    add_switch_callback(23lu, INTERRUPT_TYPE_FALLING_EDGE, sw_cb_bluetooth);
    add_switch_callback(22lu, INTERRUPT_TYPE_FALLING_EDGE, sw_cb_pause_play);
    add_switch_callback(21lu, INTERRUPT_TYPE_FALLING_EDGE, sw_cb_prev_track);
    add_switch_callback(5lu, INTERRUPT_TYPE_FALLING_EDGE, sw_cb_next_track);
    add_switch_callback(25lu, INTERRUPT_TYPE_FALLING_EDGE, sw_cb_mute);
    set_encoder_callback(enc_cb_volume);

    init_i2s();
    init_bluetooth();
}