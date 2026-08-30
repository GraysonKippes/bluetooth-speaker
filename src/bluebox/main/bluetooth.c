#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "bluetooth.h"
#include "common.h"

/* Defines */

#define DEVICE_NAME "BlueBox"

#define RINGBUF_HIGHEST_WATER_LEVEL (32 * 1024)
#define RINGBUF_PREFETCH_WATER_LEVEL (20 * 1024)

#define SAMPLE_RATE 44100
#define DATA_SIZE (SAMPLE_RATE)

#define BDA_STR_LEN 18
#define ABS_VOLUME_MIN 0
#define ABS_VOLUME_MAX 127

/* Type Definitions */

typedef enum {
    RINGBUFFER_MODE_PROCESSING,
    RINGBUFFER_MODE_PREFETCHING,
    RINGBUFFER_MODE_DROPPING
} RingbufferMode_t;

typedef struct {
    i2s_chan_handle_t i2s_chan_handle;
    SemaphoreHandle_t semaphore;
    RingbufHandle_t ringbuf;
    RingbufferMode_t ringbuf_mode;
    TaskHandle_t i2s_tx_task_handle;
} AudioOutputState_t;

typedef struct {
    int16_t volume;
    int16_t volume_adjust_amt;
} SpeakerState_t;

/* Global Variables */

static AudioOutputState_t audio_output_state = {
    .ringbuf_mode = RINGBUFFER_MODE_DROPPING
};

static SpeakerState_t speaker_state = {
    .volume = 0,
    .volume_adjust_amt = 20
};

/* Private Function Definitions */

static char *bda2str(const esp_bd_addr_t bda, char *const pStr, const size_t size)
{
    if (!bda || !pStr || size < BDA_STR_LEN) {
        return "";
    }

    const uint8_t *const p = bda;
    snprintf(pStr, size, "%02x:%02x:%02x:%02x:%02x:%02x", p[0], p[1], p[2], p[3], p[4], p[5]);
    return pStr;
}

/* I2S FUNCTIONS */

static size_t audio_output_srv(const uint8_t *data, size_t size)
{
    size_t item_size = 0;

    if (audio_output_state.ringbuf_mode == RINGBUFFER_MODE_DROPPING) {
        vRingbufferGetInfo(audio_output_state.ringbuf, NULL, NULL, NULL, NULL, &item_size);
        if (item_size <= RINGBUF_PREFETCH_WATER_LEVEL) {
            audio_output_state.ringbuf_mode = RINGBUFFER_MODE_PROCESSING;
        }
    }

    BaseType_t sent = xRingbufferSend(audio_output_state.ringbuf, (void *)data, size, (TickType_t)0);
    if (!sent) {
        audio_output_state.ringbuf_mode = RINGBUFFER_MODE_DROPPING;
    }

    if (audio_output_state.ringbuf_mode == RINGBUFFER_MODE_PREFETCHING) {
        vRingbufferGetInfo(audio_output_state.ringbuf, NULL, NULL, NULL, NULL, &item_size);
        if (item_size >= RINGBUF_PREFETCH_WATER_LEVEL) {
            audio_output_state.ringbuf_mode = RINGBUFFER_MODE_PROCESSING;
            if (!xSemaphoreGive(audio_output_state.semaphore)) {
                // error message
            }
        }
    }

    return sent ? size : 0;
}

static void i2s_tx_task(void *pParam)
{
    uint8_t *data = NULL;
    size_t item_size = 0;
    const size_t item_size_upto = 240 * 6;
    size_t bytes_written = 0;

    while (true) {
        if (xSemaphoreTake(audio_output_state.semaphore, portMAX_DELAY)) {
            while (true) {
                item_size = 0;
                data = (uint8_t *)xRingbufferReceiveUpTo(audio_output_state.ringbuf, &item_size, (TickType_t)pdMS_TO_TICKS(20), item_size_upto);
                if (item_size == 0) {
                    audio_output_state.ringbuf_mode = RINGBUFFER_MODE_PREFETCHING;
                    break;
                }

                i2s_channel_write(audio_output_state.i2s_chan_handle, data, item_size, &bytes_written, 1000);
                vRingbufferReturnItem(audio_output_state.ringbuf, (void *)data);
            }
        }
    }
}

void init_i2s(void)
{
    i2s_chan_config_t tx_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    i2s_new_channel(&tx_config, &audio_output_state.i2s_chan_handle, NULL);

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_32,
            .ws = GPIO_NUM_33, // LRCLK
            .dout = GPIO_NUM_35,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };
    i2s_channel_init_std_mode(audio_output_state.i2s_chan_handle, &std_config);
    i2s_channel_enable(audio_output_state.i2s_chan_handle);

    // Create DAC output semaphore
    audio_output_state.semaphore = xSemaphoreCreateBinary();
    if (!audio_output_state.semaphore) {
        ESP_LOGE(LOG_TAG, "failed to create audio output semaphore.");
    }

    // Create ring buffer
    audio_output_state.ringbuf = xRingbufferCreate(RINGBUF_HIGHEST_WATER_LEVEL, RINGBUF_TYPE_BYTEBUF);
    if (!audio_output_state.ringbuf) {
        ESP_LOGE(LOG_TAG, "failed to create ringbuffer.");
    }
    audio_output_state.ringbuf_mode = RINGBUFFER_MODE_PREFETCHING;

    xTaskCreate(i2s_tx_task, "i2s_tx_task", 4096, NULL, 5, &audio_output_state.i2s_tx_task_handle);
}

/* GAP FUNCTIONS */

static void init_gap(void)
{
    esp_err_t result = ESP_OK;

    result = esp_bt_gap_set_device_name(DEVICE_NAME);
    if (result != ESP_OK) {
        ESP_LOGE(LOG_TAG, "failed to set GAP device name: reason unspecified.");
    }

    const esp_bt_cod_t cod = {
        .major = ESP_BT_COD_MAJOR_DEV_AV,
        .minor = 0x07,
        .service = ESP_BT_COD_SRVC_AUDIO
    };
    result = esp_bt_gap_set_cod(cod, ESP_BT_INIT_COD);
    switch (result) {
        case ESP_ERR_INVALID_ARG:
            ESP_LOGE(LOG_TAG, "failed to set GAP class-of-device: invalid argument.");
            break;
        case ESP_ERR_INVALID_STATE:
            ESP_LOGE(LOG_TAG, "failed to set GAP class-of-device: Bluetooth stack not yet enabled.");
            break;
        case ESP_FAIL:
            ESP_LOGE(LOG_TAG, "failed to set GAP class-of-device: reason unspecified.");
            break;
    }

    result = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    switch (result) {
        case ESP_ERR_INVALID_ARG:
            ESP_LOGE(LOG_TAG, "failed to set GAP scan mode: invalid argument.");
            break;
        case ESP_ERR_INVALID_STATE:
            ESP_LOGE(LOG_TAG, "failed to set GAP scan mode: Bluetooth stack not yet enabled.");
            break;
        case ESP_FAIL:
            ESP_LOGE(LOG_TAG, "failed to set GAP scan mode: reason unspecified.");
            break;
    }

    ESP_LOGI(LOG_TAG, "initialized GAP.");
}

/* A2DP FUNCTIONS */

// A2DP profile callback.
static void a2d_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    //ESP_LOGI(LOG_TAG, "a2d_callback event");
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            ESP_LOGI(LOG_TAG, "a2d_callback event: ESP_A2D_CONNECTION_STATE_EVT");
            break;
        case ESP_A2D_AUDIO_STATE_EVT:
            ESP_LOGI(LOG_TAG, "a2d_callback event: ESP_A2D_AUDIO_STATE_EVT");
            break;
        case ESP_A2D_AUDIO_CFG_EVT:
            ESP_LOGI(LOG_TAG, "a2d_callback event: ESP_A2D_AUDIO_CFG_EVT");
            break;
        case ESP_A2D_MEDIA_CTRL_ACK_EVT:
            ESP_LOGI(LOG_TAG, "a2d_callback event: ESP_A2D_MEDIA_CTRL_ACK_EVT");
            break;
        case ESP_A2D_PROF_STATE_EVT:
            ESP_LOGI(LOG_TAG, "a2d_callback event: ESP_A2D_PROF_STATE_EVT");
            break;
        case ESP_A2D_SEP_REG_STATE_EVT:
            ESP_LOGI(LOG_TAG, "a2d_callback event: ESP_A2D_SEP_REG_STATE_EVT");
            break;
        case ESP_A2D_SNK_PSC_CFG_EVT:
            ESP_LOGI(LOG_TAG, "a2d_callback event: ESP_A2D_SNK_PSC_CFG_EVT");
            break;
        case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
            ESP_LOGI(LOG_TAG, "a2d_callback event: ESP_A2D_SNK_SET_DELAY_VALUE_EVT");
            break;
        case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:
            ESP_LOGI(LOG_TAG, "a2d_callback event: ESP_A2D_SNK_GET_DELAY_VALUE_EVT");
            break;
        case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT:
        case ESP_A2D_REPORT_SNK_CODEC_CAPS_EVT:
        case ESP_A2D_SRC_SET_PREF_MCC_EVT:
        default:
            break;
    }
}

static void audio_data_callback(const uint8_t *const data, const uint32_t len)
{
    audio_output_srv(data, len);
}

/* AVRC FUNCTIONS */

static void rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
        case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
            uint8_t *bda = param->conn_stat.remote_bda;
            char str[18];
            ESP_LOGI(LOG_TAG, "AVRC CT connection state event: state %i, address %s.", param->conn_stat.connected, bda2str(bda, str, sizeof(str)));
            break;
        }
        case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT: 
            ESP_LOGI(LOG_TAG, "AVRC CT passthrough response event.");
            break;
        case ESP_AVRC_CT_METADATA_RSP_EVT:
            ESP_LOGI(LOG_TAG, "AVRC CT metadata response event.");
            break;
        case ESP_AVRC_CT_PLAY_STATUS_RSP_EVT:
            ESP_LOGI(LOG_TAG, "AVRC CT play status response event.");
            break;
        case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
            ESP_LOGI(LOG_TAG, "AVRC CT change notification event.");
            break;
        case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
            ESP_LOGI(LOG_TAG, "AVRC CT remote features %"PRIx32", TG features %x", param->rmt_feats.feat_mask, param->rmt_feats.tg_feat_flag);
            break;
        case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
            ESP_LOGI(LOG_TAG, "AVRC CT get rn capabilities response event.");
            break;
        case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT:
            ESP_LOGI(LOG_TAG, "AVRC CT set absolute volume response event.");
            break;
        case ESP_AVRC_CT_COVER_ART_STATE_EVT:
            ESP_LOGI(LOG_TAG, "AVRC CT cover art state event.");
            break;
        case ESP_AVRC_CT_COVER_ART_DATA_EVT:
            ESP_LOGI(LOG_TAG, "AVRC CT cover art data event.");
            break;
        case ESP_AVRC_CT_PROF_STATE_EVT:
            ESP_LOGI(LOG_TAG, "AVRC CT prof state event.");
            break;
        default:
            ESP_LOGE(LOG_TAG, "undefined AVRC CT event.");
            break;
    }
}

static void rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    switch (event) {
        case ESP_AVRC_TG_CONNECTION_STATE_EVT: {
            uint8_t *bda = param->conn_stat.remote_bda;
            char str[18];
            ESP_LOGI(LOG_TAG, "AVRC TG connection state event: state %i, address %s", param->conn_stat.connected, bda2str(bda, str, sizeof(str)));
            break;
        }
        case ESP_AVRC_TG_REMOTE_FEATURES_EVT:
            ESP_LOGI(LOG_TAG, "AVRC TG remote features: %"PRIx32", CT features: %x", param->rmt_feats.feat_mask, param->rmt_feats.ct_feat_flag);
            break;
        case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT:
            ESP_LOGI(LOG_TAG, "AVRC TG passthrough command event.");
            break;
        case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
            ESP_LOGI(LOG_TAG, "AVRC TG set absolute volume: %u", param->set_abs_vol.volume);
            //volume = param->set_abs_vol.volume;
            speaker_state.volume = param->set_abs_vol.volume;
            break;
        case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
            ESP_LOGI(LOG_TAG, "AVRC TG register notification event: %i, param: 0x%"PRIx32".", param->reg_ntf.event_id, param->reg_ntf.event_parameter);
            break;
        case ESP_AVRC_TG_SET_PLAYER_APP_VALUE_EVT:
            ESP_LOGI(LOG_TAG, "AVRC TG set player app value event.");
            break;
        case ESP_AVRC_TG_PROF_STATE_EVT:
            ESP_LOGI(LOG_TAG, "AVRC TG prof state event.");
            break;
        default:
            ESP_LOGE(LOG_TAG, "undefined AVRC TG event.");
            break;
    }
}

static void update_volume(const int16_t volume)
{
    //static uint8_t transaction_label = 0;

    int16_t actual_volume = volume;
    if (volume > ABS_VOLUME_MAX) {
        actual_volume = ABS_VOLUME_MAX;
    } else if (volume < ABS_VOLUME_MIN) {
        actual_volume = ABS_VOLUME_MIN;
    }

    esp_avrc_rn_param_t rn_param;
    rn_param.volume = (uint8_t)actual_volume;
    esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_CHANGED, &rn_param);

    /*const esp_err_t ret = esp_avrc_ct_send_set_absolute_volume_cmd(transaction_label, actual_volume);
    switch (ret) {
        case ESP_OK:
            speaker_state.volume = actual_volume;
            transaction_label += 1;
            if (transaction_label > 15) transaction_label = 0;
            break;
        case ESP_ERR_INVALID_STATE:
            ESP_LOGE(LOG_TAG, "failed to send AVRC controller command to set absolute volume: bluetooth stack not yet enabled.");
            break;
        case ESP_ERR_NOT_SUPPORTED:
            ESP_LOGE(LOG_TAG, "failed to send AVRC controller command to set absolute volume: event ID not supported in current implementation.");
            break;
        case ESP_FAIL:
            ESP_LOGE(LOG_TAG, "failed to send AVRC controller command to set absolute volume: other.");
            break;
    }*/
}

static void test_volume_task(void *pParam)
{
    while (true) {
        increase_volume();
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void init_bluetooth(void)
{
    char bda_str[BDA_STR_LEN] = {0};

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(LOG_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(LOG_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg)) != ESP_OK) {
        ESP_LOGE(LOG_TAG, "%s initialize bluedroid failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(LOG_TAG, "%s enable bluedroid failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    // Set up AVRC.
    ret = esp_avrc_ct_register_callback(rc_ct_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "failed to register callback for AVRC controller module.");
        return;
    }

    ret = esp_avrc_ct_init();
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "failed to initialize AVRC controller module.");
        return;
    }
    
    ret = esp_avrc_tg_register_callback(rc_tg_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "failed to register callback for AVRC target module.");
        return;
    }

    ret = esp_avrc_tg_init();
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "failed to initialize AVRC target module.");
        return;
    }

    esp_avrc_rn_evt_cap_mask_t evt_set = {0};
    //esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_PLAY_STATUS_CHANGE);
    //esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_TRACK_CHANGE);
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
    ret = esp_avrc_tg_set_rn_evt_cap(&evt_set);
    switch (ret) {
        case ESP_OK:
            break;
        case ESP_ERR_INVALID_STATE:
            ESP_LOGE(LOG_TAG, "failed to set AVRC target module event capabilities: bluetooth stack is not yet enabled.");
            return;
        case ESP_ERR_INVALID_ARG:
            ESP_LOGE(LOG_TAG, "failed to set AVRC target module event capabilities: evt_set is NULL.");
            return;
        default:
            ESP_LOGE(LOG_TAG, "failed to set AVRC target module event capabilities: other.");
            return;
    }

    // Set up A2DP profile.
    {
        const esp_err_t result = esp_a2d_register_callback(a2d_callback);
        if (result != ESP_OK) {
            ESP_LOGE(LOG_TAG, "failed to register a2d callback");
        }
    } {
        const esp_err_t result = esp_a2d_sink_init();
        if (result != ESP_OK) {
            ESP_LOGE(LOG_TAG, "failed to initialize a2d sink");
        }
    } {
        const esp_err_t result = esp_a2d_sink_register_data_callback(audio_data_callback);
        if (result != ESP_OK) {
            ESP_LOGE(LOG_TAG, "failed to register audio data callback");
        }
    }
    
    ESP_LOGI(LOG_TAG, "Own address:[%s]", bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));
    init_gap();

    //xTaskCreate(test_volume_task, "test_volume_task", 4096, NULL, 5, NULL);
}

void increase_volume(void)
{
    const int16_t new_volume = speaker_state.volume + speaker_state.volume_adjust_amt;
    update_volume(new_volume);
}

void decrease_volume(void)
{
    const int16_t new_volume = speaker_state.volume - speaker_state.volume_adjust_amt;
    update_volume(new_volume);
}