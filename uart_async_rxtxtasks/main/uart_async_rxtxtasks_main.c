/* UART asynchronous example, that uses separate RX and TX tasks

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
#include "driver/gpio.h"
#include <stdio.h>
#include "sdkconfig.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"z
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "errno.h"
#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_err.h"
#include <sys/param.h>
#include "esp_netif.h"
#include <esp_http_server.h>


static const int RX_BUF_SIZE = 1024;
static uint8_t s_led_state = 0;
static uint8_t s_led_freq = 10; //Hz
static const char *TAG = "WIFI-AP";


#define TXD_PIN (GPIO_NUM_4)
#define RXD_PIN (GPIO_NUM_5)
#define BLINK_GPIO 2
#define EXAMPLE_ESP_WIFI_SSID      "samantha_001"
#define EXAMPLE_ESP_WIFI_PASS      "11111112"
#define EXAMPLE_ESP_WIFI_CHANNEL   1
#define EXAMPLE_MAX_STA_CONN       3

typedef struct{
    unsigned char version[10]; //v1.0.0
    uint8_t config_type; //DAS-OTA1-OTA2
    uint8_t product_type; //light-connector-camera
    uint8_t initialConfigMethod; //wifi AP/ bluetooth/ esp provision
    uint8_t normalBlinkFreq; // Hz
    uint8_t fastBlinkFreq; // Hz
    uint16_t otaSchedule; // days
    unsigned char gcpIP[15]; //0.0.0.0
    uint16_t gcpTcpPort;
    uint16_t gcpMqttPort;
    uint16_t gcpFtpPort;
    uint8_t mqttKeepAlive; // second
    uint8_t tcpKeepAlive; // second
    uint8_t wifiMeshMaxLayer; //3~10 layer
}config_param_t;

typedef struct{
    uint8_t INITIAL_FIRMWARE = 1; //to check whether using default fw or ota firmware
    uint8_t CONFIGURATION_NOT_PROVISION = 1;
    uint8_t CLOUD_CONNECTION_FAILURE = 1;
    uint8_t NO_MESH_CONNECTED = 1;
    uint8_t MESH_NODE_FAILURE = 1;
    uint8_t USER_CONNECTION_FAILURE = 1;
    uint8_t SENSOR_HARDWARE_FAILURE = 1;
    uint8_t OTA_FAILURE = 1;
    uint8_t PROVISION_FAILURE = 1;
}dtc_error_t;

static esp_err_t ledOFF_handler(httpd_req_t *req)
{
	esp_err_t error;
	ESP_LOGI(TAG, "LED Turned OFF");
	// gpio_set_level(LED, 0);
	const char *response = (const char *) req->user_ctx;
	error = httpd_resp_send(req, response, strlen(response));
	if (error != ESP_OK)
	{
		ESP_LOGI(TAG, "Error %d while sending Response", error);
	}
	else ESP_LOGI(TAG, "Response sent Successfully");
	return error;
}


static esp_err_t ledON_handler(httpd_req_t *req)
{
	esp_err_t error;
	ESP_LOGI(TAG, "LED Turned ON");
	// gpio_set_level(LED, 1);
	const char *response = (const char *) req->user_ctx;
	error = httpd_resp_send(req, response, strlen(response));
	if (error != ESP_OK)
	{
		ESP_LOGI(TAG, "Error %d while sending Response", error);
	}
	else ESP_LOGI(TAG, "Response sent Successfully");
	return error;
}



static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = ledOFF_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = "<!DOCTYPE html><html>\
\
<head>\
<style>\
form {display: grid;padding: 1em; background: #f9f9f9; border: 1px solid #c1c1c1; margin: 2rem auto 0 auto; max-width: 400px; padding: 1em;}}\
form input {background: #fff;border: 1px solid #9c9c9c;}\
form button {background: lightgrey; padding: 0.7em;width: 100%; border: 0;\
label {padding: 0.5em 0.5em 0.5em 0;}\
input {padding: 0.7em;margin-bottom: 0.5rem;}\
input:focus {outline: 10px solid gold;}\
@media (min-width: 300px) {form {grid-template-columns: 200px 1fr; grid-gap: 16px;} label { text-align: right; grid-column: 1 / 2; } input, button { grid-column: 2 / 3; }}\
</style>\
</head>\
\
<body>\
<form class=\"form1\" id=\"loginForm\" action=\"\">\
\
<label for=\"SSID\">WiFi Name</label>\
<input id=\"ssid\" type=\"text\" name=\"ssid\" maxlength=\"64\" minlength=\"4\">\
\
<label for=\"Password\">Password</label>\
<input id=\"pwd\" type=\"password\" name=\"pwd\" maxlength=\"64\" minlength=\"4\">\
\
<button>Submit</button>\
</form>\
\
<script>\
document.getElementById(\"loginForm\").addEventListener(\"submit\", (e) => {e.preventDefault(); const formData = new FormData(e.target); const data = Array.from(formData.entries()).reduce((memo, pair) => ({...memo, [pair[0]]: pair[1],  }), {}); var xhr = new XMLHttpRequest(); xhr.open(\"POST\", \"http://192.168.1.1/connection\", true); xhr.setRequestHeader('Content-Type', 'application/json'); xhr.send(JSON.stringify(data)); document.getElementById(\"output\").innerHTML = JSON.stringify(data);});\
</script>\
\
</body></html>"
};

esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        // httpd_register_uri_handler(server, &ledoff);
        // httpd_register_uri_handler(server, &ledon);
        httpd_register_uri_handler(server, &root);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_webserver(*server);
        *server = NULL;
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "" join, AID=%d",
                  event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "" leave, AID=%d",
                 event->aid);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            // .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}


void init(void) {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

int sendData(const char* logName, const char* data)
{
    const int len = strlen(data);
    const int txBytes = uart_write_bytes(UART_NUM_1, data, len);
    ESP_LOGI(logName, "Wrote %d bytes", txBytes);
    return txBytes;
}

static void tx_task(void *arg)
{
    static const char *TX_TASK_TAG = "TX_TASK";
    esp_log_level_set(TX_TASK_TAG, ESP_LOG_INFO);
    while (1) {
        sendData(TX_TASK_TAG, "Hello world");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

static void rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "RX_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE+1);
    while (1) {
        const int rxBytes = uart_read_bytes(UART_NUM_1, data, RX_BUF_SIZE, 1000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            ESP_LOG_BUFFER_HEXDUMP(RX_TASK_TAG, data, rxBytes, ESP_LOG_INFO);
        }
    }
    free(data);
}

static void blink_task(void *arg)
{
    while (1) {
        gpio_set_level(BLINK_GPIO, s_led_state);
        s_led_state = !s_led_state;
        uint16_t delayPeriod = 500/s_led_freq;
        vTaskDelay(delayPeriod / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    ESP_LOGI("MAIN","SW version:%s",__TIMESTAMP__);
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);    

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *update_partition = NULL;
    ESP_LOGI("MAIN", "Running partition type %d subtype %d (offset 0x%08x) configured(0x%08x)",
             running->type, running->subtype, running->address,configured->address);
    //This return next ota partition only 0x110000 0x210000, not factory
    update_partition = esp_ota_get_next_update_partition(NULL);
    assert(update_partition != NULL);
    ESP_LOGI("MAIN", "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);
    esp_err_t err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE("MAIN", "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
    }
    ESP_LOGI("MAIN", "Prepare to restart system!");
    // esp_restart();

	static httpd_handle_t server = NULL;
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }

    nvs_stats_t nvs_stats;
    nvs_get_stats(NULL, &nvs_stats);
    ESP_LOGI("MAIN","Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
          nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);

    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &connect_handler, &server));


    
    init();
    xTaskCreate(blink_task, "blink_task", 1024*2, NULL, configMAX_PRIORITIES, NULL);
    xTaskCreate(rx_task, "uart_rx_task", 1024*2, NULL, configMAX_PRIORITIES, NULL);
    xTaskCreate(tx_task, "uart_tx_task", 1024*2, NULL, configMAX_PRIORITIES-1, NULL);
}


/*
design:

OTA whenever available update: check version and download binary
switch partition when done 
save configuration to flash
return the MAC address to server 

fast blink when no connected
normal blink when mesh connected
slow blink when mesh + cloud connected

server - user phone
server - monitoring system
server - cluster controller - leaf node 

cluster controller: 
when no connected to cloud: fast blink + open a AP to configure wifi
when connected to cloud: normal blink + connect to cloud with tcp/mqtt/ftp
receive binary wheneveer new image
keep alive 3s
no keep alive -> reset -> mark DTC connection error -> retry till success, ping to google as well, open AP for 2 mins and retry for 2 mins
onStart(): check configuration, check cloud connection, check OTA version, check provisioning data, 
onRunning(): send keep alive, wait for cloud message, send temperature to cloud.

partition:

bootloader.bin 0x1000
partition-table.bin 0x8000
nvs_default 0x9000 0x4000
ota_data_initial.bin 0xd000 0x2000
factory.bin 0x10000 size:0xF0000
ota1.bin 0x100000 size:0xF0000
ota2.bin 0x1F0000 size:0xF0000
nvs das 0x2E0000 size 0x10000
nvs ota1 0x2F0000 size 0x10000
nvs ota2 0x300000 size 0x10000
nvs dtc 0x310000 size 0x10000
nvs user 0x320000  size 0x10000
end at 0x330000

# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x4000,
otadata,  data, ota,     0xd000,  0x2000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000,  0xF0000,
ota_0,    app,  ota_0,   0x100000, 0xF0000,
ota_1,    app,  ota_1,   0x1F0000, 0xF0000,
nvs_das,    data,  nvs,   0x2E0000, 0x10000,
nvs_oa1,    data,  nvs,   0x2F0000, 0x10000,
nvs_oa2,    data,  nvs,   0x300000, 0x10000,
nvs_dtc,    data,  nvs,   0x310000, 0x10000,
nvs_usr,    data,  nvs,   0x320000, 0x10000,

*/