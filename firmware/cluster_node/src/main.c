#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/sockets.h"  // 引入 Socket（套接字，网络通信接口）相关的库
#include "lwip/inet.h"

// ⚠️ 当前板子的节点编号（烧录2号板子改为2，3号板子改为3）
#define NODE_ID        3 

#define WIFI_SSID      "Redmi K30"
#define WIFI_PASSWORD  "445567889"

// UDP 广播（向局域网大喊）的端口，随便定一个不冲突的数字
#define DISCOVERY_PORT 8888

static const char *TAG = "Node3"; // 日志标签（烧录2号板子改成 "Node2"，以此类推）
static bool wifi_connected = false;
static esp_ip4_addr_t my_ip;

// 定义一个结构体（Struct，一种自定义的数据类型），用来记录发现的节点信息
typedef struct {
    int id;
    char ip[16];
} node_info_t;

// 定义一个简单的“节点表”，最多记录 5 个邻居
#define MAX_NODES 5
static node_info_t discovered_nodes[MAX_NODES];
static int node_count = 0;

// 记录或更新节点信息
void update_node_table(int id, char* ip) {
    // 先看看是不是已经记录过了
    for (int i = 0; i < node_count; i++) {
        if (discovered_nodes[i].id == id) {
            strcpy(discovered_nodes[i].ip, ip);
            return;
        }
    }
    // 如果是新节点，就加到表里
    if (node_count < MAX_NODES) {
        discovered_nodes[node_count].id = id;
        strcpy(discovered_nodes[node_count].ip, ip);
        node_count++;
        ESP_LOGI(TAG, "🎉 发现新节点！Node ID: %d, IP: %s", id, ip);
    }
}

// ================= WiFi 初始化（沿用之前的代码） =================
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        my_ip = event->ip_info.ip;
        wifi_connected = true;
        ESP_LOGI(TAG, "✅ WiFi 连接成功！IP: " IPSTR, IP2STR(&my_ip));
        
        // 连接成功后，关闭 WiFi 省电模式，降低延迟（这对 Raft 心跳非常重要）
        esp_wifi_set_ps(WIFI_PS_NONE); 
    }
}

void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// ================= FreeRTOS 任务：UDP 广播与监听 =================

// 任务1：广播任务（大喊“我在这里”）
void udp_broadcast_task(void *pvParameters)
{
    // 创建一个 UDP Socket（套接字）
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "创建 Socket 失败");
        vTaskDelete(NULL);
    }

    // 开启广播权限
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    // 设置目标地址为广播地址 255.255.255.255
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(DISCOVERY_PORT); // htons 把端口转成网络字节序
    dest_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    char payload[64];
    
    while (1) {
        if (wifi_connected) {
            // 拼装我们要喊出去的数据：自定义格式 "NODE_DISCOVERY:节点ID:IP地址"
            sprintf(payload, "NODE_DISCOVERY:%d:" IPSTR, NODE_ID, IP2STR(&my_ip));
            
            // 发送广播
            sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            ESP_LOGI(TAG, "📢 已发送广播: %s", payload);
        }
        vTaskDelay(pdMS_TO_TICKS(3000)); // 每 3 秒喊一次
    }
}

// 任务2：监听任务（竖着耳朵听别人喊话）
void udp_listen_task(void *pvParameters)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "创建监听 Socket 失败");
        vTaskDelete(NULL);
    }

    // 绑定（Bind）到我们的监听端口 8888
    struct sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(DISCOVERY_PORT);
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听本机所有网卡

    if (bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        ESP_LOGE(TAG, "绑定端口失败");
        vTaskDelete(NULL);
    }

    char rx_buffer[128];
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    while (1) {
        // 阻塞在这里，直到收到一条 UDP 消息
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
        if (len > 0) {
            rx_buffer[len] = '\0'; // 加上字符串结束符
            
            // 解析收到的数据：格式应为 "NODE_DISCOVERY:节点ID:IP地址"
            int sender_id = 0;
            char sender_ip[16];
            if (sscanf(rx_buffer, "NODE_DISCOVERY:%d:%s", &sender_id, sender_ip) == 2) {
                // 如果发广播的不是自己，就记录到表里
                if (sender_id != NODE_ID) {
                    update_node_table(sender_id, sender_ip);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // 小睡一下，避免占满 CPU
    }
}

// ================= 主程序 =================
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "正在连接 WiFi: %s", WIFI_SSID);
    wifi_init_sta();

    // 等待 WiFi 连接成功
    while (!wifi_connected) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // 创建两个 FreeRTOS 任务，开始分布式组网
    xTaskCreate(udp_broadcast_task, "broadcast_task", 4096, NULL, 5, NULL);
    xTaskCreate(udp_listen_task, "listen_task", 4096, NULL, 5, NULL);

    int heartbeat_count = 0;
    while (1) {
        ESP_LOGI(TAG, "心跳 #%d | 已知节点数: %d", heartbeat_count++, node_count);
        vTaskDelay(pdMS_TO_TICKS(5000)); // 主循环每 5 秒打印一次状态
    }
}