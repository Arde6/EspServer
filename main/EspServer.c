#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include <mdns.h>
#include "secrets.h"
#include "esp_spiffs.h"

#define MAX_MESSAGES 10
#define MAX_MSG_LEN 64
#define MAX_USER_LEN 20
#define WEBSOCKET_MAX_CLIENTS 4

typedef struct {
    char user[MAX_USER_LEN];
    char text[MAX_MSG_LEN];
} chat_message_t;

static chat_message_t chat_history[MAX_MESSAGES];
static int message_count = 0;
static httpd_handle_t server_handle = NULL;
static QueueHandle_t broadcast_queue = NULL;

static const char *TAG = "HTTP_SERVER";

// ----- Broadcast queue and task -----

// Structure for the broadcast queue
typedef struct {
    char payload[MAX_MSG_LEN + MAX_USER_LEN + 32];
} broadcast_msg_t;

void broadcast_task(void *pvParameters) {
    broadcast_msg_t msg;
    while (1) {
        if (xQueueReceive(broadcast_queue, &msg, portMAX_DELAY)) {
            if (!server_handle) continue;

            size_t clients = WEBSOCKET_MAX_CLIENTS;
            int client_fds[WEBSOCKET_MAX_CLIENTS] = {0};
            
            if (httpd_get_client_list(server_handle, &clients, client_fds) == ESP_OK) {
                for (size_t i = 0; i < clients; i++) {
                    if (httpd_ws_get_fd_info(server_handle, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                        httpd_ws_frame_t ws_pkt;
                        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                        ws_pkt.payload = (uint8_t*)msg.payload;
                        ws_pkt.len = strlen(msg.payload);
                        ws_pkt.type = HTTPD_WS_TYPE_TEXT;
                        
                        // Synchronous send is safe here because we are on a separate task
                        httpd_ws_send_data(server_handle, client_fds[i], &ws_pkt);
                    }
                }
            }
        }
    }
}

void queue_broadcast(const char* payload) {
    if (broadcast_queue) {
        broadcast_msg_t msg;
        strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
        msg.payload[sizeof(msg.payload) - 1] = '\0';
        xQueueSend(broadcast_queue, &msg, 0);
    }
}

// ----- Init spiffs for storing html code -----

void init_spiffs(void) {
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 10,
      .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        printf("Failed to mount or format SPIFFS\n");
    }
}

// ----- mDNS for .local address ------

void start_mdns_service(void) {
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGE(TAG, "MDNS Init failed: %d", err);
        return;
    }

    char hostname[20] = "s3super";
    mdns_hostname_set(hostname);
    mdns_instance_name_set("Ambient Sensor");

    ESP_LOGI(TAG, "MDNS service started with hostname: %s.local", hostname);
}

// ----- Wifi init and event handler -----

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Retrying to connect to WiFi...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        start_mdns_service();
    }
}

static void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &wifi_event_handler,
                                                      NULL,
                                                      &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &wifi_event_handler,
                                                      NULL,
                                                      &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = SSID,
            .password = PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// ----- Chat / Broadcast stuff -----

void send_chat_history_to_req(httpd_req_t *req) {
    char *response = malloc(2048); 
    if (!response) return;
    
    strcpy(response, "[");
    for (int i = 0; i < message_count; i++) {
        char item[128];
        snprintf(item, sizeof(item), "{\"user\":\"%s\",\"text\":\"%s\"}%s",
                 chat_history[i].user, chat_history[i].text, 
                 (i == message_count - 1) ? "" : ",");
        strcat(response, item);
    }
    strcat(response, "]");

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)response;
    ws_pkt.len = strlen(response);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    httpd_ws_send_frame(req, &ws_pkt);
    free(response);
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "New WebSocket client connected");
        send_chat_history_to_req(req);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len > 0) {
        buf = calloc(1, ws_pkt.len + 1);
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            free(buf);
            return ret;
        }

        ESP_LOGI(TAG, "Received message: %s", ws_pkt.payload);

        if (message_count >= MAX_MESSAGES) {
            for (int i = 0; i < MAX_MESSAGES - 1; i++) {
                chat_history[i] = chat_history[i + 1];
            }
            message_count = MAX_MESSAGES - 1;
        }

        char *user_ptr = strstr((char*)ws_pkt.payload, "\"user\":\"");
        char *text_ptr = strstr((char*)ws_pkt.payload, "\"text\":\"");
        
        if (user_ptr && text_ptr) {
            sscanf(user_ptr, "\"user\":\"%[^\"]\"", chat_history[message_count].user);
            sscanf(text_ptr, "\"text\":\"%[^\"]\"", chat_history[message_count].text);
            message_count++;
        }

        queue_broadcast((char*)ws_pkt.payload);
        free(buf);
    }
    return ESP_OK;
}

static const httpd_uri_t ws_uri = {
    .uri          = "/ws",
    .method       = HTTP_GET,
    .handler      = ws_handler,
    .user_ctx     = NULL,
    .is_websocket = true
};

// ----- Get files from spiffs/ for the web page -----

static esp_err_t get_content_type(const char *filename, char *content_type, size_t max_len) {
    if (strstr(filename, ".html")) {
        snprintf(content_type, max_len, "text/html");
    } else if (strstr(filename, ".css")) {
        snprintf(content_type, max_len, "text/css");
    } else if (strstr(filename, ".js")) {
        snprintf(content_type, max_len, "application/javascript");
    } else if (strstr(filename, ".png")) {
        snprintf(content_type, max_len, "image/png");
    } else if (strstr(filename, ".ico")) {
        snprintf(content_type, max_len, "image/x-icon");
    } else {
        snprintf(content_type, max_len, "text/plain");
    }
    return ESP_OK;
}

static esp_err_t common_get_handler(httpd_req_t *req) {
    char filepath[520];

    if (strcmp(req->uri, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "/spiffs/index.html");
    } else {
        snprintf(filepath, sizeof(filepath), "/spiffs%s", req->uri);
    }

    FILE* f = fopen(filepath, "r");
    if (f ==  NULL) {
        ESP_LOGE(TAG, "File not found: %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Serving file: %s", filepath);

    char content_type[64];
    get_content_type(filepath, content_type, sizeof(content_type));
    httpd_resp_set_type(req, content_type);

    char buffer[1024];
    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        httpd_resp_send_chunk(req, buffer, read_bytes);
        taskYIELD(); // Prevent starving other tasks
    }

    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return ESP_OK;
}

static httpd_uri_t common_uri = {
    .uri        = "/*",
    .method     = HTTP_GET,
    .handler    = common_get_handler,
    .user_ctx   = NULL
};

// ----- Web server starter -----
static httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    ESP_LOGI(TAG, "Starting webserver...");
    esp_err_t ret = httpd_start(&server, &config);
    if (ret == ESP_OK) {
        server_handle = server;
        httpd_register_uri_handler(server, &ws_uri);
        httpd_register_uri_handler(server, &common_uri);
        ESP_LOGI(TAG, "Webserver started successfully");
    } else {
        ESP_LOGE(TAG, "Failed to start webserver: %d", ret);
    }
    return server;
}

int startBroadcast() {
    // Probably should add error handling but whatever
    broadcast_queue = xQueueCreate(5, sizeof(broadcast_msg_t));
    xTaskCreate(broadcast_task, "broadcast_task", 4096, NULL, 5, NULL);

    return 0;
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    init_spiffs();
    startBroadcast();
    wifi_init_sta();
    start_webserver();
}
