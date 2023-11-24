#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "user_interface.h"

#define DIFF_PARTITION_NAME "diff" /*!< 差分文件区的分区名 */
#define TAG "diff_OTA"

static uint32_t i = 0;

// 任务函数实现
void print_task(void *pvParameters)
{
    while (1)
    {
        ESP_LOGI("print_task", "i = %d", i++);
        vTaskDelay(pdMS_TO_TICKS(200));

        if (i == 20)
        {
            ESP_LOGI(TAG, "退出任务");
            break;
        }
    }

    vTaskDelete(NULL);
}

void app_main()
{
    // 初始化NVS
    ESP_LOGI(TAG, "初始化NVS");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 创建一个任务
    xTaskCreate(&print_task, "print_task", 2048, NULL, 5, NULL);

    while (1)
    {
        if (i == 20)
        {
            ESP_LOGI(TAG, "进入");
            diff_OTA("diff");
            i = 21;
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "退出");
}
