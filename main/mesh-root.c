// #include <stdio.h>
//#include "sdkconfig.h"
//#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"
//#include "esp_spi_flash.h"

#include <cJSON.h>
#include <mqtt_client.h>
#include <string.h>
#include "device.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_netif.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"

/*******************************************************
 *                Constants
 *******************************************************/
#define CONFIG_MESH_ROUTER_SSID             "TMT"
#define CONFIG_MESH_ROUTER_PASSWD           "tmt123123@"
#define CONFIG_MESH_CHANNEL                 0
#define CONFIG_MESH_AP_PASSWD               "minhtaile2712"
#define CONFIG_MESH_MAX_LAYER               6
#define CONFIG_MESH_AP_CONNECTIONS          6
#define CONFIG_MESH_AP_AUTHMODE             3
#define CONFIG_MESH_NON_MESH_AP_CONNECTIONS 0
#define CONFIG_MESH_TOPOLOGY                0
/*******************************************************
 *                Variable Definitions
 *******************************************************/
static const char* MESH_TAG     = "mesh_main";
static const char* MQTT_TAG     = "mqtt_main";
static const uint8_t MESH_ID[6] = {0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
static mesh_addr_t mesh_parent_addr;
static char* cmd_topic;
static char* root_addr_str;
static int mesh_layer         = -1;
static esp_netif_t* netif_sta = NULL;
static EventGroupHandle_t event_group;
static esp_mqtt_client_handle_t mqtt_client;
///////////////////////////////////////
void mesh_event_handler(void* arg,
                        esp_event_base_t event_base,
                        int32_t event_id,
                        void* event_data) {
    mesh_addr_t id = {
        0,
    };
    static uint16_t last_layer = 0;

    switch (event_id) {
        case MESH_EVENT_STARTED: {
            esp_mesh_get_id(&id);
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:" MACSTR "",
                     MAC2STR(id.addr));
            mesh_layer = esp_mesh_get_layer();
        } break;
        case MESH_EVENT_STOPPED: {
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
            mesh_layer = esp_mesh_get_layer();
        } break;
        case MESH_EVENT_CHILD_CONNECTED: {
            mesh_event_child_connected_t* child_connected =
                (mesh_event_child_connected_t*)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, " MACSTR "",
                     child_connected->aid, MAC2STR(child_connected->mac));
        } break;
        case MESH_EVENT_CHILD_DISCONNECTED: {
            mesh_event_child_disconnected_t* child_disconnected =
                (mesh_event_child_disconnected_t*)event_data;
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, " MACSTR "",
                     child_disconnected->aid, MAC2STR(child_disconnected->mac));
        } break;
        case MESH_EVENT_ROUTING_TABLE_ADD: {
            mesh_event_routing_table_change_t* routing_table =
                (mesh_event_routing_table_change_t*)event_data;
            ESP_LOGW(MESH_TAG,
                     "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d, layer:%d",
                     routing_table->rt_size_change, routing_table->rt_size_new,
                     mesh_layer);
        } break;
        case MESH_EVENT_ROUTING_TABLE_REMOVE: {
            mesh_event_routing_table_change_t* routing_table =
                (mesh_event_routing_table_change_t*)event_data;
            ESP_LOGW(
                MESH_TAG,
                "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d, layer:%d",
                routing_table->rt_size_change, routing_table->rt_size_new,
                mesh_layer);
        } break;
        case MESH_EVENT_NO_PARENT_FOUND: {
            mesh_event_no_parent_found_t* no_parent =
                (mesh_event_no_parent_found_t*)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
                     no_parent->scan_times);
        }
        /* TODO handler for the failure */
        break;
        case MESH_EVENT_PARENT_CONNECTED: {
            mesh_event_connected_t* connected =
                (mesh_event_connected_t*)event_data;
            esp_mesh_get_id(&id);
            mesh_layer = connected->self_layer;
            memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
            ESP_LOGI(
                MESH_TAG,
                "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:" MACSTR
                "%s, ID:" MACSTR ", duty:%d",
                last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                esp_mesh_is_root()  ? "<ROOT>"
                : (mesh_layer == 2) ? "<layer2>"
                                    : "",
                MAC2STR(id.addr), connected->duty);
            last_layer = mesh_layer;
            //		mesh_connected_indicator(mesh_layer);
            if (esp_mesh_is_root()) {
                esp_netif_dhcpc_stop(netif_sta);
                esp_netif_dhcpc_start(netif_sta);
            }
        } break;
        case MESH_EVENT_PARENT_DISCONNECTED: {
            mesh_event_disconnected_t* disconnected =
                (mesh_event_disconnected_t*)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                     disconnected->reason);
            //		mesh_disconnected_indicator();
            mesh_layer = esp_mesh_get_layer();
        } break;
        case MESH_EVENT_LAYER_CHANGE: {
            mesh_event_layer_change_t* layer_change =
                (mesh_event_layer_change_t*)event_data;
            mesh_layer = layer_change->new_layer;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s",
                     last_layer, mesh_layer,
                     esp_mesh_is_root()  ? "<ROOT>"
                     : (mesh_layer == 2) ? "<layer2>"
                                         : "");
            last_layer = mesh_layer;
            //		mesh_connected_indicator(mesh_layer);
        } break;
        case MESH_EVENT_ROOT_ADDRESS: {
            mesh_event_root_address_t* root_addr =
                (mesh_event_root_address_t*)event_data;
            cmd_topic = malloc(17);
            sprintf(cmd_topic, "cmd/%02X%02X%02X%02X%02X%02X",
                    MAC2STR(root_addr->addr));
            root_addr_str = malloc(13);
            sprintf(root_addr_str, "%02X%02X%02X%02X%02X%02X",
                    MAC2STR(root_addr->addr));
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_ROOT_ADDRESS>root address:" MACSTR "",
                     MAC2STR(root_addr->addr));
        } break;
        case MESH_EVENT_VOTE_STARTED: {
            mesh_event_vote_started_t* vote_started =
                (mesh_event_vote_started_t*)event_data;
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_VOTE_STARTED>attempts:%d, reason:%d, "
                     "rc_addr:" MACSTR "",
                     vote_started->attempts, vote_started->reason,
                     MAC2STR(vote_started->rc_addr.addr));
        } break;
        case MESH_EVENT_VOTE_STOPPED: {
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_VOTE_STOPPED>");
            break;
        }
        case MESH_EVENT_ROOT_SWITCH_REQ: {
            mesh_event_root_switch_req_t* switch_req =
                (mesh_event_root_switch_req_t*)event_data;
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_ROOT_SWITCH_REQ>reason:%d, rc_addr:" MACSTR
                     "",
                     switch_req->reason, MAC2STR(switch_req->rc_addr.addr));
        } break;
        case MESH_EVENT_ROOT_SWITCH_ACK: {
            /* new root */
            mesh_layer = esp_mesh_get_layer();
            esp_mesh_get_parent_bssid(&mesh_parent_addr);
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_ROOT_SWITCH_ACK>layer:%d, parent:" MACSTR "",
                     mesh_layer, MAC2STR(mesh_parent_addr.addr));
        } break;
        case MESH_EVENT_TODS_STATE: {
            mesh_event_toDS_state_t* toDs_state =
                (mesh_event_toDS_state_t*)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_TODS_REACHABLE>state:%d",
                     *toDs_state);
        } break;
        case MESH_EVENT_ROOT_FIXED: {
            mesh_event_root_fixed_t* root_fixed =
                (mesh_event_root_fixed_t*)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_FIXED>%s",
                     root_fixed->is_fixed ? "fixed" : "not fixed");
        } break;
        case MESH_EVENT_ROOT_ASKED_YIELD: {
            mesh_event_root_conflict_t* root_conflict =
                (mesh_event_root_conflict_t*)event_data;
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_ROOT_ASKED_YIELD>" MACSTR
                     ", rssi:%d, capacity:%d",
                     MAC2STR(root_conflict->addr), root_conflict->rssi,
                     root_conflict->capacity);
        } break;
        case MESH_EVENT_CHANNEL_SWITCH: {
            mesh_event_channel_switch_t* channel_switch =
                (mesh_event_channel_switch_t*)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHANNEL_SWITCH>new channel:%d",
                     channel_switch->channel);
        } break;
        case MESH_EVENT_SCAN_DONE: {
            mesh_event_scan_done_t* scan_done =
                (mesh_event_scan_done_t*)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_SCAN_DONE>number:%d",
                     scan_done->number);
        } break;
        case MESH_EVENT_NETWORK_STATE: {
            mesh_event_network_state_t* network_state =
                (mesh_event_network_state_t*)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_NETWORK_STATE>is_rootless:%d",
                     network_state->is_rootless);
        } break;
        case MESH_EVENT_STOP_RECONNECTION: {
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOP_RECONNECTION>");
        } break;
        case MESH_EVENT_FIND_NETWORK: {
            mesh_event_find_network_t* find_network =
                (mesh_event_find_network_t*)event_data;
            ESP_LOGI(
                MESH_TAG,
                "<MESH_EVENT_FIND_NETWORK>new channel:%d, router BSSID:" MACSTR
                "",
                find_network->channel, MAC2STR(find_network->router_bssid));
        } break;
        case MESH_EVENT_ROUTER_SWITCH: {
            mesh_event_router_switch_t* router_switch =
                (mesh_event_router_switch_t*)event_data;
            ESP_LOGI(
                MESH_TAG,
                "<MESH_EVENT_ROUTER_SWITCH>new router:%s, channel:%d, " MACSTR
                "",
                router_switch->ssid, router_switch->channel,
                MAC2STR(router_switch->bssid));
        } break;
        case MESH_EVENT_PS_PARENT_DUTY: {
            mesh_event_ps_duty_t* ps_duty = (mesh_event_ps_duty_t*)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_PARENT_DUTY>duty:%d",
                     ps_duty->duty);
        } break;
        case MESH_EVENT_PS_CHILD_DUTY: {
            mesh_event_ps_duty_t* ps_duty = (mesh_event_ps_duty_t*)event_data;
            ESP_LOGI(MESH_TAG,
                     "<MESH_EVENT_PS_CHILD_DUTY>cidx:%d, " MACSTR ", duty:%d",
                     ps_duty->child_connected.aid - 1,
                     MAC2STR(ps_duty->child_connected.mac), ps_duty->duty);
        } break;
        default:
            ESP_LOGI(MESH_TAG, "unknown id:%d", event_id);
            break;
    }
}

static void ip_event_handler(void* arg,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void* event_data) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
    ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR,
             IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(event_group, 1 << 6);
}

void send_to_node(mesh_addr_t node_addr, cJSON* data) {
    char* str = cJSON_PrintUnformatted(data);
    mesh_data_t send_data;
    send_data.data  = (uint8_t*)str;
    send_data.size  = strlen(str);
    send_data.proto = MESH_PROTO_BIN;
    send_data.tos   = MESH_DATA_FROMDS;
    ESP_ERROR_CHECK(
        esp_mesh_send(&node_addr, &send_data, MESH_DATA_FROMDS, NULL, 0));
    // printf("%s\n", esp_err_to_name(err));
}

static void _mqtt_data_handler(char* topic, char* data) {
    cJSON* data_json = cJSON_Parse(data);
    printf("Data receive from server %s\n", cJSON_PrintUnformatted(data_json));
    if (strcmp(topic, cmd_topic) == 0) {
        cJSON* device_id_object = cJSON_GetObjectItem(data_json, "device_id");
        if (cJSON_IsString(device_id_object)) {
            char* device_id_str = device_id_object->valuestring;
            if (strcmp(device_id_str, root_addr_str) == 0) {
            } else {
                mesh_addr_t mesh_child_addr;
                cJSON* channel_data =
                    cJSON_GetObjectItem(data_json, "channels");
                unsigned int bytearray[6];
                for (int i = 0; i < 6; i++) {
                    sscanf(device_id_str + 2 * i, "%02X", &bytearray[i]);
                    mesh_child_addr.addr[i] = bytearray[i];
                }
                send_to_node(mesh_child_addr, channel_data);
            }
        }
    }
    cJSON_Delete(data_json);
}

void MQTT_event_handler(void* arg,
                        esp_event_base_t event_base,
                        int32_t event_id,
                        void* event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED: {
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_CONNECTED");
            xEventGroupSetBits(event_group, 1 << 7);
            char* mqtt_prov_data = device_get_mqtt_provision_json_data();
            esp_mqtt_client_publish(mqtt_client, "provision", mqtt_prov_data, 0,
                                    1, 0);
            esp_mqtt_client_subscribe(mqtt_client, cmd_topic, 0);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_SUBSCRIBED: {
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_SUBSCRIBED");
            break;
        }
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_UNSUBSCRIBED");
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_PUBLISHED");
            break;
        case MQTT_EVENT_DATA: {
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_DATA");

            /* Allocate memory for data */
            char* topic = malloc(event->topic_len + 1);
            char* data  = malloc(event->data_len + 1);

            sprintf(topic, "%.*s", event->topic_len, event->topic);
            sprintf(data, "%.*s", event->data_len, event->data);

            /* Handle received data */
            _mqtt_data_handler(topic, data);

            /* Free memory after handling data */
            free(topic);
            free(data);
            break;
        }
        case MQTT_EVENT_ERROR:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(MQTT_TAG, "Other event id:%d", event->event_id);
            break;
    }
}

void mqtt_publish(const char* topic, cJSON* data) {
    esp_mqtt_client_publish(mqtt_client, topic, cJSON_PrintUnformatted(data), 0,
                            1, 0);
}

void response_control() {
    printf("Toggle main power\n");
}

void config_mesh_root(void) {
    ESP_ERROR_CHECK(nvs_flash_init());

    /*  tcpip initialization */
    ESP_ERROR_CHECK(esp_netif_init());

    /*  event initialization */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /*  create network interfaces for mesh (only station instance saved for
     * further manipulation, soft AP instance ignored */
    ESP_ERROR_CHECK(
        esp_netif_create_default_wifi_mesh_netifs(&netif_sta, NULL));

    /*  Wi-Fi initialization */
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());

    /*  mesh initialization */
    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID,
                                               &mesh_event_handler, NULL));
    /*  set mesh topology */
    ESP_ERROR_CHECK(esp_mesh_set_topology(CONFIG_MESH_TOPOLOGY));
    /*  set mesh max layer according to the topology */
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
    ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(128));

    /* Disable mesh PS function */
    ESP_ERROR_CHECK(esp_mesh_disable_ps());
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));

    /* Enable the Mesh IE encryption by default */
    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();

    /* mesh ID */
    memcpy((uint8_t*)&cfg.mesh_id, MESH_ID, 6);

    /* router */
    /* channel (must match the router's channel) */
    cfg.channel         = CONFIG_MESH_CHANNEL;
    cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
    memcpy((uint8_t*)&cfg.router.ssid, CONFIG_MESH_ROUTER_SSID,
           cfg.router.ssid_len);
    memcpy((uint8_t*)&cfg.router.password, CONFIG_MESH_ROUTER_PASSWD,
           strlen(CONFIG_MESH_ROUTER_PASSWD));

    /* mesh softAP */
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE));
    cfg.mesh_ap.max_connection         = CONFIG_MESH_AP_CONNECTIONS;
    cfg.mesh_ap.nonmesh_max_connection = CONFIG_MESH_NON_MESH_AP_CONNECTIONS;
    memcpy((uint8_t*)&cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD,
           strlen(CONFIG_MESH_AP_PASSWD));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));

    /* mesh start */
    ESP_ERROR_CHECK(esp_mesh_start());

    ESP_LOGI(
        MESH_TAG, "mesh starts successfully, heap:%d, %s<%d>%s, ps: % d\n ",
        esp_get_minimum_free_heap_size(),
        esp_mesh_is_root_fixed() ? "root fixed" : "root not fixed",
        esp_mesh_get_topology(), esp_mesh_get_topology() ? "(chain)" : "(tree)",
        esp_mesh_is_ps_enabled());
    /////////////////////////////////////////////////////////////////////
    event_group = xEventGroupCreate();
    xEventGroupWaitBits(event_group, 1 << 6, true, false, portMAX_DELAY);
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "mqtt://172.29.5.92",
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                   MQTT_event_handler, mqtt_client);
    esp_mqtt_client_start(mqtt_client);
    ESP_ERROR_CHECK(esp_event_handler_register("MQTT_EVENTS", ESP_EVENT_ANY_ID,
                                               &MQTT_event_handler, NULL));
    xEventGroupWaitBits(event_group, 1 << 7, true, false, portMAX_DELAY);
}