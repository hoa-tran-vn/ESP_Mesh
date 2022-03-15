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

void send_to_root(cJSON* data);
void response_control();
bool provision_device();
void config_mesh_node(void);