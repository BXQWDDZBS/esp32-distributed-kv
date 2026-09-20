/* ============================================================
 * 功能简介：
 * 这是一个 ESP32 / ESP-IDF 的 WiFi STA 模式示例。
 * STA 模式表示 ESP32 像手机一样，去连接一个 WiFi 热点。
 * 连接成功后，会每隔 2 秒打印一次“心跳”信息，包括 IP 和剩余内存。
 * ============================================================ */

/* 标准输入输出库，保留即可 */
#include <stdio.h>

/* 字符串处理库，保留即可 */
#include <string.h>

/* FreeRTOS 基础头文件，提供任务、延时等基础功能 */
#include "freertos/FreeRTOS.h"

/* FreeRTOS 任务相关 API，例如 vTaskDelay */
#include "freertos/task.h"

/* ESP-IDF 系统相关 API，例如 esp_get_free_heap_size */
#include "esp_system.h"

/* WiFi 相关 API，例如 esp_wifi_init、esp_wifi_connect */
#include "esp_wifi.h"

/* 事件循环相关 API，用于注册 WiFi / IP 事件回调 */
#include "esp_event.h"

/* 日志输出 API，例如 ESP_LOGI、ESP_LOGW */
#include "esp_log.h"

/* NVS 非易失性存储初始化，WiFi 驱动会用到 */
#include "nvs_flash.h"

/* 网络接口相关 API，例如 esp_netif_init */
#include "esp_netif.h"

/* ==================== 用户配置区 ==================== */

// ⚠️ 修改 1：节点编号（烧录 2 号板子时改为 2，3 号板子改为 3）
#define NODE_ID  1

/* 下面两个宏是你要连接的 WiFi 名称和密码。
 * 注意：ESP32 通常只支持 2.4GHz WiFi，手机热点建议设置为 2.4GHz。
 * 如果连接失败，优先检查 SSID、密码、热点频段。 */
// ⚠️ 改成你手机热点的名字和密码！
#define WIFI_SSID      "Redmi K30"
#define WIFI_PASSWORD  "445567889"

/* 日志标签：ESP_LOGI(TAG, ...) 输出日志时会显示这个标签，方便区分节点 */
static const char *TAG = "Node1";

/* 记录当前 WiFi 是否已经连接成功，并且拿到了 IP 地址 */
static bool wifi_connected = false;

/* 保存本机从路由器 / 手机热点获取到的 IPv4 地址 */
static esp_ip4_addr_t my_ip;

/* ============================================================
 * WiFi 事件处理回调函数
 * 当 WiFi 状态变化，或者成功获取 IP 时，ESP-IDF 会自动调用这个函数。
 *
 * 参数说明：
 *   arg        ：注册时传入的用户参数，这里没有使用
 *   event_base ：事件类别，例如 WIFI_EVENT、IP_EVENT
 *   event_id   ：具体事件编号
 *   event_data ：事件附带的数据
 * ============================================================ */
// WiFi 事件处理回调函数
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    /* 如果发生的是 WiFi 事件，并且事件是 STA 启动完成 */
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        /* STA 启动后，主动去连接 WiFi */
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        /* 如果 WiFi 断开了，先把连接标志设为 false */
        wifi_connected = false;

        /* 打印日志：提示正在重连 */
        ESP_LOGI(TAG, "WiFi 断开连接，正在重连...");

        /* 再次尝试连接 WiFi */
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        /* 如果成功获取到 IP，把事件数据转换成 got_ip 事件结构体 */
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;

        /* 保存获取到的 IP 地址 */
        my_ip = event->ip_info.ip;

        /* 标记 WiFi 已连接成功 */
        wifi_connected = true;

        /* 打印成功日志。IPSTR 和 IP2STR 是 ESP-IDF 用来格式化 IPv4 地址的宏 */
        ESP_LOGI(TAG, "✅ WiFi 连接成功！获取到 IP 地址: " IPSTR, IP2STR(&my_ip));
    }
}

/* ============================================================
 * 初始化 WiFi，并让 ESP32 进入 STA 模式
 * STA = Station，表示 ESP32 作为客户端去连接热点 / 路由器。
 * ============================================================ */
// 初始化 WiFi（STA 模式）
void wifi_init_sta(void)
{
    /* 初始化网络接口层，ESP-IDF 的网络功能需要先初始化 netif */
    ESP_ERROR_CHECK(esp_netif_init());

    /* 创建默认事件循环，WiFi 和 IP 事件需要事件循环来分发 */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 创建默认的 WiFi STA 网络接口 */
    esp_netif_create_default_wifi_sta();

    /* 使用默认配置初始化 WiFi 驱动 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 注册 WiFi 事件处理函数：WIFI_EVENT 的任意事件都会进入 wifi_event_handler */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    /* 注册 IP 事件处理函数：这里只关心“获取到 IP”的事件 */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    /* 准备 WiFi 配置结构体，使用指定初始化器，只填写 .sta 部分 */
    wifi_config_t wifi_config = {
        /* .sta 表示作为 STA 客户端时的配置 */
        .sta = {
            /* 要连接的热点名称 */
            .ssid = WIFI_SSID,

            /* 热点密码 */
            .password = WIFI_PASSWORD,

            /* 最低认证模式：WPA2-PSK */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    /* 设置 WiFi 模式为 STA，也就是客户端模式 */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    /* 把上面的 WiFi 配置应用到 STA 接口 */
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    /* 启动 WiFi。启动后会触发 WIFI_EVENT_STA_START 事件 */
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ============================================================
 * 程序入口
 * ESP-IDF 启动后会自动调用 app_main 函数。
 * ============================================================ */
void app_main(void)
{
    /* 1. 初始化 NVS（非易失性存储，WiFi 需要用到） */

    /* nvs_flash_init 返回 esp_err_t 类型，用来表示成功或失败 */
    esp_err_t ret = nvs_flash_init();

    /* 如果 NVS 没有空闲页，或者版本不匹配，就先擦除再初始化 */
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* 擦除 NVS */
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* 再次初始化 NVS */
        ret = nvs_flash_init();
    }

    /* 检查最终初始化结果，如果失败会打印错误并中止 */
    ESP_ERROR_CHECK(ret);

    /* 2. 连接 WiFi */

    /* 打印正在连接的 WiFi 名称 */
    ESP_LOGI(TAG, "正在连接 WiFi: %s", WIFI_SSID);

    /* 调用上面写好的 WiFi 初始化函数 */
    wifi_init_sta();

    /* 心跳计数，每打印一次就加 1 */
    int heartbeat_count = 0;

    /* 3. 主循环 */
    while (1) {
        /* 如果 WiFi 已经连接成功 */
        if (wifi_connected) {
            /* 打印心跳信息：计数、IP 地址、剩余堆内存 */
            ESP_LOGI(TAG, "心跳 #%d | IP: " IPSTR " | Free RAM: %lu bytes", 
                     heartbeat_count++, IP2STR(&my_ip), (unsigned long)esp_get_free_heap_size());
        } else {
            /* 如果还没连接成功，就打印等待日志 */
            ESP_LOGW(TAG, "等待 WiFi 连接中...");
        }

        /* 延时 2000 毫秒，也就是 2 秒。
         * vTaskDelay 的参数单位是 tick，
         * pdMS_TO_TICKS(2000) 会把 2000 毫秒转换成对应的 tick 数。 */
        vTaskDelay(pdMS_TO_TICKS(2000)); // 每 2 秒打印一次心跳
    }
}