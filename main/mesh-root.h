#include <cJSON.h>
#include <mqtt_client.h>
#include <string.h>
#include "device.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"

void send_to_node(mesh_addr_t node_addr, cJSON* data);
void mqtt_publish(const char* topic, cJSON* data);
void response_control();
void config_mesh_root(void);