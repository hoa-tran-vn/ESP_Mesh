#include <cJSON.h>
#include <stdio.h>
#include "device.h"
#include "driver/gpio.h"
#include "esp_mesh.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
// #include "mesh-node.h"
#include "mesh-root.h"
#include "sdkconfig.h"

#define RELAY_1  16
#define BUTTON_1 17
#define RELAY_2  18
#define BUTTON_2 19
#define RELAY_3  22
#define BUTTON_3 23
#define RELAY_4  25
#define BUTTON_4 26
#define RELAY_5  27
#define BUTTON_5 14
#define RELAY_6  32
#define BUTTON_6 33

#define RELAY_MASK                                              \
    (1ULL << RELAY_1) | (1ULL << RELAY_2) | (1ULL << RELAY_3) | \
        (1ULL << RELAY_4) | (1ULL << RELAY_5) | (1ULL << RELAY_6)
#define BUTTON_MASK                                                \
    (1ULL << BUTTON_1) | (1ULL << BUTTON_2) | (1ULL << BUTTON_3) | \
        (1ULL << BUTTON_4) | (1ULL << BUTTON_5) | (1ULL << BUTTON_6)

typedef struct {
    uint8_t relay_io;
    uint8_t button_io;
    int8_t device_state;
    bool is_handling;
} relay_device_t;

relay_device_t device_list[6];

// static QueueHandle_t gpio_evt_queue = NULL;
static EventGroupHandle_t event_group;

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t)arg;
    int8_t bit        = -1;
    if (gpio_num == BUTTON_1) bit = 0;
    else if (gpio_num == BUTTON_2) bit = 1;
    else if (gpio_num == BUTTON_3) bit = 2;
    else if (gpio_num == BUTTON_4) bit = 3;
    else if (gpio_num == BUTTON_5) bit = 4;
    else if (gpio_num == BUTTON_6) bit = 5;

    if (bit != -1) {
        xEventGroupSetBitsFromISR(event_group, 1 << bit, NULL);
    }
}

void toggle_device_2(uint8_t num) {
    uint32_t button_io = device_list[num].button_io;
    uint32_t relay_io  = device_list[num].relay_io;
    bool is_handling   = device_list[num].is_handling;
    if ((!gpio_get_level(button_io)) && (!is_handling)) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        if (!gpio_get_level(button_io)) {
            device_list[num].is_handling  = 1;
            device_list[num].device_state = 1 - device_list[num].device_state;
            gpio_set_level(relay_io, device_list[num].device_state);
            char temp[10];
            sprintf(temp, "relay_%d", num + 1);
            device_set_channel_value(temp, &(device_list[num].device_state));
            response_control();
        }
    } else if ((gpio_get_level(button_io)) && (is_handling)) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        xEventGroupClearBits(event_group, 1 << num);
        device_list[num].is_handling = 0;
    }
}

static void soft_button_handler(void* arg) {
    EventBits_t uxBits;
    for (;;) {
        uxBits = xEventGroupWaitBits(
            event_group,
            (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5),
            pdFALSE, pdFALSE, portMAX_DELAY);
        if ((uxBits & (1 << 0)) != 0) {
            toggle_device_2(0);
        }
        if ((uxBits & (1 << 1)) != 0) {
            toggle_device_2(1);
        }
        if ((uxBits & (1 << 2)) != 0) {
            toggle_device_2(2);
        }
        if ((uxBits & (1 << 3)) != 0) {
            toggle_device_2(3);
        }
        if ((uxBits & (1 << 4)) != 0) {
            toggle_device_2(4);
        }
        if ((uxBits & (1 << 5)) != 0) {
            toggle_device_2(5);
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void config_gpio_init() {
    gpio_config_t relay_cfg = {.mode         = GPIO_MODE_INPUT_OUTPUT,
                               .pin_bit_mask = RELAY_MASK,
                               .pull_down_en = 0,
                               .pull_up_en   = 0};
    gpio_config(&relay_cfg);
    gpio_set_level(RELAY_1, 0);
    gpio_set_level(RELAY_2, 0);
    gpio_set_level(RELAY_3, 0);
    gpio_set_level(RELAY_4, 0);
    gpio_set_level(RELAY_5, 0);
    gpio_set_level(RELAY_6, 0);
}

void define_devices() {
    device_list[0].relay_io    = RELAY_1;
    device_list[0].button_io   = BUTTON_1;
    device_list[0].is_handling = 0;

    device_list[1].relay_io    = RELAY_2;
    device_list[1].button_io   = BUTTON_2;
    device_list[1].is_handling = 0;

    device_list[2].relay_io    = RELAY_3;
    device_list[2].button_io   = BUTTON_3;
    device_list[2].is_handling = 0;

    device_list[3].relay_io    = RELAY_4;
    device_list[3].button_io   = BUTTON_4;
    device_list[3].is_handling = 0;

    device_list[4].relay_io    = RELAY_5;
    device_list[4].button_io   = BUTTON_5;
    device_list[4].is_handling = 0;

    device_list[5].relay_io    = RELAY_6;
    device_list[5].button_io   = BUTTON_6;
    device_list[5].is_handling = 0;
}

uint8_t detect_connected_module() {
    gpio_config_t button_cfg = {.intr_type    = GPIO_INTR_NEGEDGE,
                                .mode         = GPIO_MODE_OUTPUT,
                                .pin_bit_mask = BUTTON_MASK,
                                .pull_down_en = 0,
                                .pull_up_en   = 1};
    gpio_config(&button_cfg);
    gpio_set_level(BUTTON_1, 0);
    gpio_set_level(BUTTON_2, 0);
    gpio_set_level(BUTTON_3, 0);
    gpio_set_level(BUTTON_4, 0);
    gpio_set_level(BUTTON_5, 0);
    gpio_set_level(BUTTON_6, 0);
    vTaskDelay(1 / portTICK_PERIOD_MS);
    uint8_t count = 0;
    for (int i = 0; i < 6; i++) {
        gpio_set_direction(device_list[i].button_io, GPIO_MODE_INPUT);
        vTaskDelay(1 / portTICK_PERIOD_MS);
        if (!gpio_get_level(device_list[i].button_io)) {
            count++;
            device_list[i].device_state =
                gpio_get_level(device_list[i].relay_io);
        } else device_list[i].device_state = -1;
    }
    button_cfg.mode = GPIO_MODE_INPUT;
    gpio_config(&button_cfg);
    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
    for (int i = 0; i < 6; i++) {
        if (device_list[i].device_state != -1)
            gpio_isr_handler_add(device_list[i].button_io, gpio_isr_handler,
                                 (void*)(device_list[i].button_io));
    }

    return count;
}

void turn_on(gpio_num_t gpio_num) {
    if (!gpio_get_level(gpio_num)) {
        gpio_set_level(gpio_num, 1);
        if (gpio_num == RELAY_1) device_list[0].device_state = 1;
        else if (gpio_num == RELAY_2) device_list[1].device_state = 1;
        else if (gpio_num == RELAY_3) device_list[2].device_state = 1;
        else if (gpio_num == RELAY_4) device_list[3].device_state = 1;
        else if (gpio_num == RELAY_5) device_list[4].device_state = 1;
        else if (gpio_num == RELAY_6) device_list[5].device_state = 1;
    }
}

void turn_off(gpio_num_t gpio_num) {
    if (gpio_get_level(gpio_num)) {
        gpio_set_level(gpio_num, 0);
        if (gpio_num == RELAY_1) device_list[0].device_state = 0;
        else if (gpio_num == RELAY_2) device_list[1].device_state = 0;
        else if (gpio_num == RELAY_3) device_list[2].device_state = 0;
        else if (gpio_num == RELAY_4) device_list[3].device_state = 0;
        else if (gpio_num == RELAY_5) device_list[4].device_state = 0;
        else if (gpio_num == RELAY_6) device_list[5].device_state = 0;
    }
}

void turn_all_on() {
    if (!device_list[0].device_state) turn_on(RELAY_1);
    if (!device_list[1].device_state) turn_on(RELAY_2);
    if (!device_list[2].device_state) turn_on(RELAY_3);
    if (!device_list[3].device_state) turn_on(RELAY_4);
    if (!device_list[4].device_state) turn_on(RELAY_5);
    if (!device_list[5].device_state) turn_on(RELAY_6);
}

void turn_all_off() {
    if (device_list[0].device_state) turn_off(RELAY_1);
    if (device_list[1].device_state) turn_off(RELAY_2);
    if (device_list[2].device_state) turn_off(RELAY_3);
    if (device_list[3].device_state) turn_off(RELAY_4);
    if (device_list[4].device_state) turn_off(RELAY_5);
    if (device_list[5].device_state) turn_off(RELAY_6);
}

void create_device_channel() {
    char temp[10];
    device_init("relay");
    for (uint8_t i = 0; i < 6; i++) {
        sprintf(temp, "relay_%d", i + 1);
        if (device_list[i].device_state != -1) {
            device_add_bool_channel(temp, true, "", "");
        } else {
            device_add_bool_channel(temp, false, "", "");
        }
        device_set_channel_value(temp, &(device_list[i].device_state));
    }
}

static void mesh_root_receive(void* arg) {
    mesh_addr_t src;
    mesh_data_t recv_data;
    esp_err_t err;
    int flag       = 0;
    cJSON* data    = cJSON_CreateObject();
    recv_data.data = data;
    for (;;) {
        recv_data.size = 2048;
        err = esp_mesh_recv(&src, &recv_data, portMAX_DELAY, &flag, NULL, 0);
        if (err == ESP_OK) {
            printf("*********Receive*********\n");
            data = cJSON_Parse((char*)(recv_data.data));
            mqtt_publish("response", data);
        } else printf("Receive error 0x%x\n", err);
        if (cJSON_IsObject(data)) cJSON_Delete(data);
    }
}

static void mesh_node_receive(void* arg) {
    mesh_addr_t src;
    mesh_data_t recv_data;
    esp_err_t err;
    int flag           = 0;
    cJSON* relay_state = NULL;
    cJSON* data        = cJSON_CreateObject();
    recv_data.data     = data;
    for (;;) {
        recv_data.size = 2048;
        err = esp_mesh_recv(&src, &recv_data, portMAX_DELAY, &flag, NULL, 0);
        printf("*********Receive*********\n");
        if (err == ESP_OK) {
            data = cJSON_Parse((char*)(recv_data.data));
            if (!esp_mesh_is_root()) {
                relay_state = cJSON_GetObjectItem(data, "relay_1");
                if (cJSON_IsBool(relay_state)) {
                    if (relay_state->valueint) {
                        turn_on(RELAY_1);
                    } else {
                        turn_off(RELAY_1);
                    }
                    device_set_channel_value("relay_1",
                                             &(device_list[0].device_state));
                }
                relay_state = cJSON_GetObjectItem(data, "relay_2");
                if (cJSON_IsBool(relay_state)) {
                    if (relay_state->valueint) {
                        turn_on(RELAY_2);
                    } else {
                        turn_off(RELAY_2);
                    }
                    device_set_channel_value("relay_2",
                                             &(device_list[1].device_state));
                }
                relay_state = cJSON_GetObjectItem(data, "relay_3");
                if (cJSON_IsBool(relay_state)) {
                    if (relay_state->valueint) {
                        turn_on(RELAY_3);
                    } else {
                        turn_off(RELAY_3);
                    }
                    device_set_channel_value("relay_3",
                                             &(device_list[2].device_state));
                }
                relay_state = cJSON_GetObjectItem(data, "relay_4");
                if (cJSON_IsBool(relay_state)) {
                    if (relay_state->valueint) {
                        turn_on(RELAY_4);
                    } else {
                        turn_off(RELAY_4);
                    }
                    device_set_channel_value("relay_4",
                                             &(device_list[3].device_state));
                }
                relay_state = cJSON_GetObjectItem(data, "relay_5");
                if (cJSON_IsBool(relay_state)) {
                    if (relay_state->valueint) {
                        turn_on(RELAY_5);
                    } else {
                        turn_off(RELAY_5);
                    }
                    device_set_channel_value("relay_5",
                                             &(device_list[4].device_state));
                }
                relay_state = cJSON_GetObjectItem(data, "relay_6");
                if (cJSON_IsBool(relay_state)) {
                    if (relay_state->valueint) {
                        turn_on(RELAY_6);
                    } else {
                        turn_off(RELAY_6);
                    }
                    device_set_channel_value("relay_6",
                                             &(device_list[5].device_state));
                }
                if (cJSON_IsObject(data)) response_control();
            }
        } else printf("Receive error 0x%x\n", err);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        if (cJSON_IsObject(data)) cJSON_Delete(data);
    }
}

void app_main() {
    config_gpio_init();
    define_devices();
    detect_connected_module();
    event_group = xEventGroupCreate();
    xTaskCreate(soft_button_handler, "soft_button", 2048, NULL, 10, NULL);

    config_mesh_root();
    xTaskCreate(mesh_root_receive, "receive", 8192, NULL, 5, NULL);

    // config_mesh_node();
    // xTaskCreate(mesh_node_receive, "receive", 8192, NULL, 5, NULL);

    create_device_channel();
    while (1) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}
