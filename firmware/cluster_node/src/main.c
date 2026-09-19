#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

// 这是 ESP-IDF 的入口函数（相当于普通 C 语言的 main）
void app_main(void)
{
    printf("\n=========================================\n");
    printf("🚀 Cluster Node 1 is Booting...\n");
    printf("Hardware: ESP32-C3 (RISC-V)\n");
    printf("=========================================\n");

    int heartbeat_count = 0;
    
    // 这是一个死循环，模拟心跳机制
    while (1) {
        printf("[Node 1] Heartbeat: %d | Free RAM: %lu bytes\n", 
               heartbeat_count++, (unsigned long)esp_get_free_heap_size());
        
        // 延时 1000 毫秒（1秒），让出 CPU 给其他任务（FreeRTOS 核心概念）
        vTaskDelay(1000 / portTICK_PERIOD_MS); 
    }
}