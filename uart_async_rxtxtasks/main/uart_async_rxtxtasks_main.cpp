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
#include "esp_event.h"
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
#include "lwip/inet.h"
#include "esp_err.h"
#include <sys/param.h>
#include "esp_netif.h"
#include <esp_http_server.h>
#include <esp_mac.h>
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "mqtt_client.h"
#include "ping/ping_sock.h"
#include "esp_heap_caps.h"

static const int RX_BUF_SIZE = 1024;
static uint8_t s_led_state = 0;
static uint8_t s_led_freq = 10; //Hz
static const char *WIFITAG = "-----[WIFI-AP]";
static const char *WIFISTA = "-----[WIFI-STA]";
static const char *MAINTAG = "-----[MAIN]";
static const char *MQTTTAG = "-----[MQTT]";
static const char *ICMPTAG = "-----[PING]";

#define SW_VERSION_NUMBER "v0.1.7"
#define SW_VERSION_LENGTH 22
#define NVS_DEFAULT_NAMESPACE "default_nspc"
#define NVS_DEFAULT_KEY "nvs_key"
#define NVS_PTT_DAS "nvs_das"
#define NVS_PTT_OA1 "nvs_oa1"
#define NVS_PTT_OA2 "nvs_oa2"
#define NVS_PTT_DTC "nvs_dtc"
#define MAC_ADDRESS_LENGTH 8
#define DTC_UNSET 0
#define DTC_SET   1
#define DEFAULT_RSSI -127
#define DEFAULT_AUTHMODE WIFI_AUTH_WPA2_PSK
#define DEFAULT_SCAN_METHOD WIFI_FAST_SCAN
#define DEFAULT_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL

#define WIFI_SSID "HIDDEN__NETWORK"
#define WIFI_PASSWD "11111112"

#define CONFIG_BROKER_URL "34.126.97.74"
#define CONFIG_BROKER_PORT 1883
#define MQTT_CLIENT_ID_LENGTH 18 //ESP32_AABBCCDDEEFF

// #define WIFI_SSID "zzzzz"
// #define WIFI_PASSWD "11111123"
// enum{
//     MQTT_EVENT = 121,
// }spec_event_t;

typedef enum {
    MQTT_STATE_INIT = 0,
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_WAIT_RECONNECT,
} mqtt_client_state_t;

ESP_EVENT_DECLARE_BASE(MQTT_EVENT);
ESP_EVENT_DEFINE_BASE(MQTT_EVENT);

static char SW_VERSION_FULL[SW_VERSION_LENGTH]={0,}; //v1.01.01-yymmdd.hhmmss
static char MQTT_CLIENT_ID[MQTT_CLIENT_ID_LENGTH]={0,}; //ESP32_AABBCCDDEEFF
static char MQTT_TOPIC_V[40]={0,}; // /b/clientid/default
static char MQTT_TOPIC_B[40]={0,}; // /v/clientid/default
#define MQTT_APP_DEFAULT "default"
#define MQTT_APP_LIGHT "light"

esp_mqtt_client_handle_t mClient;
static mqtt_client_state_t mMqttState = MQTT_STATE_INIT;
uint16_t sensorData = 0;


#define TXD_PIN (GPIO_NUM_4)
#define RXD_PIN (GPIO_NUM_5)
#define BLINK_GPIO (gpio_num_t)2
#define EXAMPLE_ESP_WIFI_SSID      "samantha_001"
#define EXAMPLE_ESP_WIFI_PASS      "11111112"
#define EXAMPLE_ESP_WIFI_CHANNEL   1
#define EXAMPLE_MAX_STA_CONN       3

typedef struct{
    unsigned char swVer[SW_VERSION_LENGTH]; //v1.01.01-yymmdd.hhmmss
    unsigned char macAddress[MAC_ADDRESS_LENGTH]; //v1.01.01-yymmdd.hhmmss
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

config_param_t GLOBAL_DAS = {
    "NA", // version1
    "NA",  // version2
    0,  // config_type
    1,  // product_type
    2,  // initialConfigMethod
    1,  // normalBlinkFreq
    5,  // fastBlinkFreq
    30,  // otaSchedule
    {0,},  // gcpIP
    8891,  // gcpTcpPort
    8884,  // gcpMqttPort
    8882,  // gcpFtpPort
    5,  // mqttKeepAlive
    3,  // tcpKeepAlive
    4  // wifiMeshMaxLayer
};

typedef struct{
    uint8_t INITIAL_FIRMWARE; //= 1; to check whether using default fw or ota firmware
    uint8_t PROVISION_FAILURE ;//= 1;
    uint8_t OTA_FAILURE ;//= 1;
    uint8_t CLOUD_CONNECTION_FAILURE ;//= 1;
    uint8_t NO_MESH_CONNECTED ;//= 1;
    uint8_t MESH_NODE_FAILURE ;//= 1;
    uint8_t USER_CONNECTION_FAILURE ;//= 1;
    uint8_t SENSOR_HARDWARE_FAILURE ;//= 1;
}dtc_error_t;

dtc_error_t GLOBAL_DTC = {
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1
};

void initMqttConnection(void);
void initIcmp();

static esp_err_t ledOFF_handler(httpd_req_t *req)
{
	esp_err_t error;
	ESP_LOGI(WIFITAG, "LED Turned OFF");
	// gpio_set_level(LED, 0);
	const char *response = (const char *) req->user_ctx;
	error = httpd_resp_send(req, response, strlen(response));
	if (error != ESP_OK)
	{
		ESP_LOGI(WIFITAG, "Error %d while sending Response", error);
	}
	else ESP_LOGI(WIFITAG, "Response sent Successfully");
	return error;
}


static esp_err_t ledON_handler(httpd_req_t *req)
{
	esp_err_t error;
	ESP_LOGI(WIFITAG, "LED Turned ON");
	// gpio_set_level(LED, 1);
	const char *response = (const char *) req->user_ctx;
	error = httpd_resp_send(req, response, strlen(response));
	if (error != ESP_OK)
	{
		ESP_LOGI(WIFITAG, "Error %d while sending Response", error);
	}
	else ESP_LOGI(WIFITAG, "Response sent Successfully");
	return error;
}

static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = ledOFF_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = (void *) "<!DOCTYPE html><html>\
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
    ESP_LOGI(WIFITAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(WIFITAG, "Registering URI handlers");
        // httpd_register_uri_handler(server, &ledoff);
        // httpd_register_uri_handler(server, &ledon);
        httpd_register_uri_handler(server, &root);
        return server;
    }

    ESP_LOGI(WIFITAG, "Error starting server!");
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
        ESP_LOGI(WIFITAG, "Stopping webserver");
        stop_webserver(*server);
        *server = NULL;
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(WIFITAG, "Starting webserver");
        *server = start_webserver();
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(WIFITAG, "station "" join, AID=%d",
                  event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(WIFITAG, "station "" leave, AID=%d",
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
            // .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .max_connection = EXAMPLE_MAX_STA_CONN
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(WIFITAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

static void test_on_ping_success(esp_ping_handle_t hdl, void *args)
{
    // optionally, get callback arguments
    // const char* str = (const char*) args;
    // printf("%s\r\n", str); // "foo"
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    ESP_LOGI(ICMPTAG, "%d bytes from %s icmp_seq=%d ttl=%d time=%d ms",
           (int)recv_len, inet_ntoa(target_addr.u_addr.ip4), (int)seqno, (int)ttl, (int)elapsed_time);

}

static void test_on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    uint16_t seqno;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    ESP_LOGI(ICMPTAG, "From %s icmp_seq=%d timeout", inet_ntoa(target_addr.u_addr.ip4), (int)seqno);
}

static void test_on_ping_end(esp_ping_handle_t hdl, void *args)
{
    uint32_t transmitted;
    uint32_t received;
    uint32_t total_time_ms;

    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    ESP_LOGI(ICMPTAG, "%d packets transmitted, %d received, time %dms", (int)transmitted, (int)received, (int)total_time_ms);
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if( event_base == WIFI_EVENT){
        if(event_id == WIFI_EVENT_STA_START){
            ESP_LOGI(WIFISTA, "WIFI_EVENT_STA_START");
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
            ESP_LOGI(WIFISTA, "WIFI_EVENT_STA_DISCONNECTED:%d",event->reason);
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT){
        if(event_id == IP_EVENT_STA_GOT_IP){
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(WIFISTA, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
            initMqttConnection();
            initIcmp();
        }
    } 

}

void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGI(MQTTTAG, "Event dispatched from event loop base=%s, event_id=%d", base, (int)event_id);
    esp_mqtt_event_handle_t event= (esp_mqtt_event_handle_t) event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        mMqttState = MQTT_STATE_CONNECTED;
        ESP_LOGI(MQTTTAG, "MQTT_EVENT_CONNECTED");

        msg_id = esp_mqtt_client_publish(client, MQTT_TOPIC_B, "Client has connected", 0, 1, 0);
        ESP_LOGI(MQTTTAG, "sent publish successful, msg_id=%d", (int)msg_id);
        msg_id = esp_mqtt_client_subscribe(client,  MQTT_TOPIC_V, 0);
        ESP_LOGI(MQTTTAG, "sent subscribe successful, msg_id=%d", (int)msg_id);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(MQTTTAG, "MQTT_EVENT_DISCONNECTED");
        mMqttState = MQTT_STATE_DISCONNECTED;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(MQTTTAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/b/AaBbCcDdEeFf/default", "data", 0, 0, 0);
        ESP_LOGI(MQTTTAG, "sent publish successful, msg_id=%d", msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(MQTTTAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(MQTTTAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(MQTTTAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(MQTTTAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGI(MQTTTAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(MQTTTAG, "Other event id:%d", event->event_id);
        break;
    }
}


/* Initialize Wi-Fi as sta and set scan method */
static void initWifiStation(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    // Initialize default station as network interface instance (esp-netif)
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWD,
        },
    };
    ESP_LOGI(WIFISTA, "fast_scan . SSID:%s password:%s ",
             wifi_config.sta.ssid, wifi_config.sta.password);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

esp_err_t nvsReadSize(const char *part_name, const char* key, size_t* length ){
    nvs_handle_t my_handle;
    esp_err_t err;
    char err_msg[20];

    nvs_stats_t nvs_stats;
    nvs_get_stats(part_name, &nvs_stats);

    if(nvs_stats.used_entries <= 0){
        ESP_LOGI(MAINTAG,"[%s] partition %s is empty",__FUNCTION__,part_name);
        err = ESP_ERR_NOT_FOUND;
        return err;
    }
    ESP_LOGI(MAINTAG,"[%s] partition %s has %d/%d",__FUNCTION__,part_name, nvs_stats.used_entries,nvs_stats.total_entries);

    err = nvs_open_from_partition(part_name, NVS_DEFAULT_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK){
        ESP_LOGI(MAINTAG,"[%s] nvs_open_from_partition failed:%s",__FUNCTION__, esp_err_to_name_r(err, err_msg, sizeof(err_msg)));
        err = ESP_ERR_INVALID_STATE;
        return err;
    }

    size_t required_size = 0;  // value will default to 0, if not set yet in NVS
    err = nvs_get_blob(my_handle, key, NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND ) return err;
    ESP_LOGI(MAINTAG,"[%s] partition %s is required %d",__FUNCTION__,part_name,required_size);

    *length = required_size;
    err= ESP_OK;
    nvs_close(my_handle);

    ESP_LOGI(MAINTAG,"[%s] Done",__FUNCTION__);
    return err;
}

esp_err_t nvsReadBlob(const char *part_name, const char* key, void* out_value, size_t* length ){
    nvs_handle_t my_handle;
    esp_err_t err;
    char err_msg[20];

    err = nvs_open_from_partition(part_name, NVS_DEFAULT_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK){
        ESP_LOGI(MAINTAG,"[%s] nvs_open_from_partition failed:%s",__FUNCTION__, esp_err_to_name_r(err, err_msg, sizeof(err_msg)));
        err = ESP_ERR_INVALID_STATE;
        return err;
    }


    err = nvs_get_blob(my_handle, key, out_value, length);
    if (err != ESP_OK) {
        ESP_LOGI(MAINTAG,"[%s] nvs_get_blob %s error:%d",__FUNCTION__, key, err);
        return err;
    }

    nvs_close(my_handle);
    err = ESP_OK;
    ESP_LOGI(MAINTAG,"[%s] Done",__FUNCTION__);
    return err;

}

esp_err_t nvsWriteBlob(const char *part_name, const char* key, void* in_value, size_t length){
    nvs_handle_t my_handle;
    esp_err_t err;
    char err_msg[20];

    err = nvs_open_from_partition(part_name, NVS_DEFAULT_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK){
        ESP_LOGI(MAINTAG,"[%s] nvs_open_from_partition failed:%s",__FUNCTION__, esp_err_to_name_r(err, err_msg, sizeof(err_msg)));
        err = ESP_ERR_INVALID_STATE;
        return err;
    }

    err = nvs_set_blob(my_handle, key, in_value, length);
    if (err != ESP_OK){
        ESP_LOGI(MAINTAG,"[%s] nvs_set_blob failed:%s",__FUNCTION__, esp_err_to_name_r(err, err_msg, sizeof(err_msg)));
        return err;
    } 

    err = nvs_commit(my_handle);
    if (err != ESP_OK){
        ESP_LOGI(MAINTAG,"[%s] nvs_commit failed:%s",__FUNCTION__, esp_err_to_name_r(err, err_msg, sizeof(err_msg)));
        return err;
    } 

    nvs_close(my_handle);
    err = ESP_OK;
    ESP_LOGI(MAINTAG,"[%s] Done",__FUNCTION__);
    return err;
}

void initVersion(void){
    char version_yymmdd[6] ={
    __DATE__[9], __DATE__[10],
    (__DATE__[0] == 'O' || __DATE__[0] == 'N' || __DATE__[0] == 'D') ? '1' : '0', //Oct Nov Dec = '1'
    (__DATE__[0] == 'J') ? ( (__DATE__[1] == 'a') ? '1' :       // Jan, Jun or Jul
                                ((__DATE__[2] == 'n') ? '6' : '7') ) :
    (__DATE__[0] == 'F') ? '2' :                                // Feb 
    (__DATE__[0] == 'M') ? (__DATE__[2] == 'r') ? '3' : '5' :   // Mar or May
    (__DATE__[0] == 'A') ? (__DATE__[1] == 'p') ? '4' : '8' :   // Apr or Aug
    (__DATE__[0] == 'S') ? '9' :                                // Sep
    (__DATE__[0] == 'O') ? '0' :                                // Oct
    (__DATE__[0] == 'N') ? '1' :                                // Nov
    (__DATE__[0] == 'D') ? '2' :                                // Dec
    0,
    __DATE__[4]==' ' ? '0' : __DATE__[4],
    __DATE__[5]
    };
    char version_hhmmss[6] ={
    __TIME__[0], __TIME__[1],
    __TIME__[3], __TIME__[4],
    __TIME__[6], __TIME__[7]};
    char version_sw[6]=
    {
        'v',
        '1','.',
        '0','.',
        '1'
    };
    uint8_t pos = 0;
    memset(&SW_VERSION_FULL[0],0,sizeof(SW_VERSION_FULL));
    // memcpy(&SW_VERSION_FULL[0],&version_sw[0],sizeof(version_sw));
    memcpy(&SW_VERSION_FULL[pos],&SW_VERSION_NUMBER[0],sizeof(SW_VERSION_NUMBER));
    pos += sizeof(SW_VERSION_NUMBER); pos -= 1;
    SW_VERSION_FULL[pos]='-';
    pos += 1; 
    memcpy(&SW_VERSION_FULL[pos],&version_yymmdd[0],sizeof(version_yymmdd));
    pos += sizeof(version_yymmdd); 
    SW_VERSION_FULL[pos]='.';
    pos += 1;
    memcpy(&SW_VERSION_FULL[pos],&version_hhmmss[0],sizeof(version_hhmmss));

    ESP_LOGI(MAINTAG,"[%s] ################################################################################################################",__FUNCTION__);
    ESP_LOGI(MAINTAG,"[%s] SW_VERSION_FULL=%s=",__FUNCTION__,SW_VERSION_FULL);
    ESP_LOGI(MAINTAG,"[%s] Done",__FUNCTION__);
}

void initUart(void) {
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
    ESP_LOGI(MAINTAG,"[%s] Done",__FUNCTION__);
}

void initLed(void) {
    gpio_reset_pin((gpio_num_t)BLINK_GPIO);
    gpio_set_direction((gpio_num_t)BLINK_GPIO, GPIO_MODE_OUTPUT);    
    ESP_LOGI(MAINTAG,"[%s] Done",__FUNCTION__);
}

void initNvs(void) {
    //Default nvs
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(nvs_flash_init_partition("nvs_das"));
    ESP_ERROR_CHECK(nvs_flash_init_partition("nvs_oa1"));
    ESP_ERROR_CHECK(nvs_flash_init_partition("nvs_oa2"));
    ESP_ERROR_CHECK(nvs_flash_init_partition("nvs_dtc"));


    nvs_stats_t nvs_stats;
    nvs_get_stats(NULL, &nvs_stats);
    ESP_LOGI(MAINTAG,"[%s] default_nvs Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)",
          __FUNCTION__, nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
    
    nvs_get_stats("nvs_das", &nvs_stats);
    ESP_LOGI(MAINTAG,"[%s] nvs_das Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)",
          __FUNCTION__, nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);

    nvs_get_stats("nvs_oa1", &nvs_stats);
    ESP_LOGI(MAINTAG,"[%s] nvs_oa1 Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)",
          __FUNCTION__, nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);

    nvs_get_stats("nvs_oa2", &nvs_stats);
    ESP_LOGI(MAINTAG,"[%s] nvs_oa2 Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)",
          __FUNCTION__, nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);

    nvs_get_stats("nvs_dtc", &nvs_stats);
    ESP_LOGI(MAINTAG,"[%s] nvs_dtc Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)",
          __FUNCTION__, nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
                              
    // nvs_das
    // nvs_oa1
    // nvs_oa2
    // nvs_dtc
    // nvs_usr
    // nvs_ext
    ESP_LOGI(MAINTAG,"[%s] Done",__FUNCTION__);
}

void initConfiguration(void){
    //factory image will write the dtc and das
    //ota image will check dtc and check das availability
    //ota image will update swver to ota1 or ota2

    esp_err_t errStat;
    config_param_t curDas = {0,};
    dtc_error_t curDtc = {0,};
    size_t length;
    
    //If no available das, write the default das to global variable
    length=sizeof(curDas);
    errStat = nvsReadBlob(NVS_PTT_DAS,NVS_DEFAULT_KEY, &curDas,&length);
    if(errStat!=ESP_OK) { 
        errStat = nvsWriteBlob(NVS_PTT_DAS,NVS_DEFAULT_KEY, &GLOBAL_DAS, sizeof(GLOBAL_DAS));
    }

    //If no available dtc, write the default dtc to global variable
    length = sizeof(curDtc);
    errStat = nvsReadBlob(NVS_PTT_DTC,NVS_DEFAULT_KEY, &curDtc,&length);
    if(errStat!=ESP_OK) { 
        errStat = nvsWriteBlob(NVS_PTT_DTC,NVS_DEFAULT_KEY, &GLOBAL_DTC, sizeof(GLOBAL_DTC));
    }

    // size_t targetRead = 0;
    // errStat = nvsReadSize("nvs_das","das_key",&targetRead);
    // if(errStat!=ESP_OK)return;

    curDas = {0,};
    length = sizeof(curDas);
    errStat = nvsReadBlob(NVS_PTT_DAS,NVS_DEFAULT_KEY, &curDas,&length);
    if(errStat!=ESP_OK) { ESP_LOGI(MAINTAG,"[%s] nvsReadBlob failed:%d",__FUNCTION__,errStat); return;}
    GLOBAL_DAS = curDas;

    curDtc = {0,};
    length = sizeof(curDtc);
    errStat = nvsReadBlob(NVS_PTT_DTC,NVS_DEFAULT_KEY, &curDtc,&length);
    if(errStat!=ESP_OK) { ESP_LOGI(MAINTAG,"[%s] nvsReadBlob failed:%d",__FUNCTION__,errStat); return;}
    GLOBAL_DTC = curDtc;

    //Write current swversion to nvs
    memcpy(&GLOBAL_DAS.swVer[0], &SW_VERSION_FULL[0], sizeof(SW_VERSION_FULL));
    ESP_LOGI(MAINTAG,"[%s] GLOBAL_DAS::SW_VERSION_FULL=%s",__FUNCTION__, GLOBAL_DAS.swVer);

    errStat = esp_read_mac(&GLOBAL_DAS.macAddress[0], ESP_MAC_WIFI_STA);
    snprintf(MQTT_CLIENT_ID, sizeof(MQTT_CLIENT_ID)+1, "ESP32_%02X%02X%02X%02X%02X%02X",
         GLOBAL_DAS.macAddress[0], GLOBAL_DAS.macAddress[1], GLOBAL_DAS.macAddress[2],
         GLOBAL_DAS.macAddress[3], GLOBAL_DAS.macAddress[4], GLOBAL_DAS.macAddress[5]);
    snprintf(MQTT_TOPIC_V, 4+sizeof(MQTT_CLIENT_ID)+sizeof(MQTT_APP_DEFAULT)+1, "/v/%s/%s",MQTT_CLIENT_ID,MQTT_APP_DEFAULT);
    snprintf(MQTT_TOPIC_B, 4+sizeof(MQTT_CLIENT_ID)+sizeof(MQTT_APP_DEFAULT)+1, "/b/%s/%s",MQTT_CLIENT_ID,MQTT_APP_DEFAULT);
    
    ESP_LOGI(MAINTAG,"[%s] GLOBAL_DAS::macAddress="MACSTR,__FUNCTION__, MAC2STR(GLOBAL_DAS.macAddress));
    ESP_LOGI(MAINTAG,"[%s] Set mqtt client id=%s",__FUNCTION__, MQTT_CLIENT_ID);

    errStat = nvsWriteBlob(NVS_PTT_DAS,NVS_DEFAULT_KEY, &GLOBAL_DAS, sizeof(GLOBAL_DAS));

    GLOBAL_DTC.INITIAL_FIRMWARE = DTC_UNSET;
    GLOBAL_DTC.OTA_FAILURE = DTC_UNSET;
    GLOBAL_DTC.PROVISION_FAILURE = DTC_UNSET;
    errStat = nvsWriteBlob(NVS_PTT_DTC,NVS_DEFAULT_KEY, &GLOBAL_DTC, sizeof(GLOBAL_DTC));

    // ESP_LOGI(MAINTAG,"[%s] erasing ",__FUNCTION__);
    // nvs_flash_erase_partition(NVS_PTT_DAS);
    // nvs_flash_erase_partition(NVS_PTT_DTC);

    ESP_LOGI(MAINTAG,"[%s] Done",__FUNCTION__);
}

void initPartitionInfo(void) {

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *update_partition = NULL;
    ESP_LOGI(MAINTAG, "Running(0x%08x) configured(0x%08x)", (unsigned int)running->address,(unsigned int)configured->address);
    //This return next ota partition only 0x110000 0x210000, not factory
    update_partition = esp_ota_get_next_update_partition(NULL);
    assert(update_partition != NULL);
    ESP_LOGI(MAINTAG, "update_partition(0x%08x) configured(0x%08x)", (unsigned int)(update_partition->address),(unsigned int)configured->address);

    ESP_LOGI(MAINTAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, (unsigned int)update_partition->address);
    esp_err_t err = esp_ota_set_boot_partition(update_partition);
    configured = esp_ota_get_boot_partition();
    if (err != ESP_OK) {
        ESP_LOGE("MAIN", "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
    }
    ESP_LOGI(MAINTAG, "Prepare to restart system! (0x%08x) ",(unsigned int)configured->address);
    
    // esp_restart();
    ESP_LOGI(MAINTAG,"[%s] Done",__FUNCTION__);
}

void initMqttConnection(void)
{
    esp_mqtt_client_config_t mqtt_cfg ={0,};
    mqtt_cfg.broker.address.hostname = CONFIG_BROKER_URL;
    mqtt_cfg.broker.address.port = CONFIG_BROKER_PORT;
    mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    mqtt_cfg.credentials.client_id = MQTT_CLIENT_ID;

    // esp_event_loop_args_t loop_without_task_args = {
    //     .queue_size = 5,
    //     .task_name = NULL // no task will be created
    // };
    // ESP_ERROR_CHECK(esp_event_loop_create(&loop_without_task_args,));
    // esp_mqtt_client_config_t mqtt_cfg  = {
    //     .broker.address.uri = "mqtt://34.126.97.74:1883",
    // };
        // .broker.address.port = CONFIG_BROKER_PORT,
    // esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    mClient = esp_mqtt_client_init(&mqtt_cfg);
    
    esp_mqtt_client_register_event(mClient, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mClient);
}

void initIcmp()
{
    ip_addr_t target_addr;
    ip4_addr_t target_addr4;
    
    ip4addr_aton(CONFIG_BROKER_URL, &target_addr4);
    memcpy(&target_addr,&target_addr4,sizeof(target_addr4));

    ESP_LOGI(ICMPTAG, "ipaddr:%s ipaddr4:%s"
        ,inet_ntoa(target_addr.u_addr.ip4),inet_ntoa(target_addr4.addr));

    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr = target_addr;          // target IP address
    ping_config.count = ESP_PING_COUNT_INFINITE;    // ping in infinite mode, esp_ping_stop can stop it

    /* set callback functions */
    esp_ping_callbacks_t cbs;
    cbs.on_ping_success = test_on_ping_success;
    cbs.on_ping_timeout = test_on_ping_timeout;
    cbs.on_ping_end = test_on_ping_end;
    // cbs.cb_args = (void*)"foo";  // arguments that will feed to all callback functions, can be NULL
    // cbs.cb_args = eth_event_group;
    cbs.cb_args = NULL;
    esp_ping_handle_t ping;
    esp_ping_new_session(&ping_config, &cbs, &ping);
    esp_ping_start(ping);
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

static void mqtt_client_task(void *arg)
{
    while (1) {
        if(mMqttState == MQTT_STATE_CONNECTED){
            char tempMsg[50]={0,};
            snprintf(tempMsg, 17, "Sensor data 1:%hu",sensorData++);
            esp_mqtt_client_publish(mClient, MQTT_TOPIC_B, tempMsg , 0, 0, 0);
            ESP_LOGI(MQTTTAG,"[%s] Sending message:%s",__FUNCTION__,tempMsg);
            ESP_LOGI(MQTTTAG,"[%s] FreeHeap:%d InternalHeap:%d Minimum free:%d ",__FUNCTION__,
                heap_caps_get_free_size( MALLOC_CAP_DEFAULT ),
                heap_caps_get_free_size( MALLOC_CAP_8BIT | MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL ),
                heap_caps_get_minimum_free_size( MALLOC_CAP_DEFAULT ));
        }
        uint16_t delayPeriod = 500/2;
        vTaskDelay(delayPeriod / portTICK_PERIOD_MS);        
    }
}

extern "C" void app_main(void)
{
    initVersion();
    initUart();
    initLed();

    initNvs();
    initConfiguration();
    initPartitionInfo();

    initWifiStation();

    ESP_LOGI(MAINTAG,"[%s] System has BOOT_COMPLETED",__FUNCTION__);

    // initSoftAp();
    // ESP_LOGI(WIFITAG, "ESP_WIFI_MODE_AP");
    // wifi_init_softap();
    // ESP_ERROR_CHECK(esp_netif_init());
    // static httpd_handle_t server = NULL;
    // ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &connect_handler, &server));


    
    xTaskCreate(blink_task, "blink_task", 1024*2, NULL, configMAX_PRIORITIES, NULL);
    xTaskCreate(rx_task, "uart_rx_task", 1024*2, NULL, configMAX_PRIORITIES, NULL);
    xTaskCreate(tx_task, "uart_tx_task", 1024*2, NULL, configMAX_PRIORITIES-1, NULL);
    xTaskCreate(mqtt_client_task, "mqtt_client_task", 1024*2, NULL, configMAX_PRIORITIES-1, NULL);
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
onStart(): check DTC, check configuration, check OTA version, check cloud connection,
onRunning(): send keep alive, wait for cloud message, send temperature to cloud.

//todo
implement cloud connection
implement event loop


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
factory,  app,  factory, 0x10000,  0xF0000,
ota_0,    app,  ota_0,   0x100000, 0xF0000,
ota_1,    app,  ota_1,   0x1F0000, 0xF0000,
nvs_das,    data,  nvs,   0x2E0000, 0x10000,
nvs_oa1,    data,  nvs,   0x2F0000, 0x10000,
nvs_oa2,    data,  nvs,   0x300000, 0x10000,
nvs_dtc,    data,  nvs,   0x310000, 0x10000,
nvs_usr,    data,  nvs,   0x320000, 0x10000,
nvs_ext,    data,  nvs,   0x330000, 0x50000,



IPC: RPC and Pub/Sub
Dirrection: ClusterController <-> Broker <-> BackendApplication
Component: multi clustercontroller, single broker, single backendApplication, single database

Detail dirrection:
- ClusterController will feed clientData to Broker via publish
- Backend will get clientData from Broker via subsribe

topic type:
b/client-id/domain
v/client-id/domain
m/domain

b... :message from ClusterController to backend 
v... :message from backend to specific ClusterController
m... :message from backend to multi ClusterController
domain is like app id domain, like default, light or switch or door
client-id is build base on macaddress aa-bb-cc-dd-ee-ff

mqtt message structure:
client-id/msgType/msgId/payload

msgType=onewaymsg/replymsg/requestmsg/publicmsg/multicastmsg/subreply/subrequest/subcancel
msgId= index of msg send to backend
payload=methodName/paramTypeList/paramList

mqtt client id:
lt=backend:dt=BE[conn.vps-prov]:cut=joynr:uci=lpvcdconnapp16.bmwgroup.net_i0_lpvcdconnapp16
Joynr Client Properties:
joynr.messaging.mqtt.clientidprefix Must be in format „lt=backend:dt=BE[<joynr-domain-name>]:cut=“ 
joynr.messaging.receiverid  Must be in format „uci=<hostname>“
Vehicle MQTT Client ID:
lt=vehicle:dt=ATM2[558926A9A7477700000000000000B3FF]:cut=joynr:uci=b6ec3cde-2319-4301-a200-3e7041bc24f2

tmperature not working
sending ping and memory remain to server
init a message queue or event loop to process message async

*/