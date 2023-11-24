/**
 * @file user_interface.c
 * @author Liu Jiaqi (jiaqi.liu@itead.cc)
 * @brief
 * @date 2023-11-21
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "user_interface.h"
#include "bspatch.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "crc32.h"
#include "string.h"

#define TAG "diff_OTA"

/**
 * @brief 将差分文件写入用户的flash,用户自己决定是否在写之前擦除
 *
 * @param addr
 * @param p
 * @param len
 * @return int
 */
int bs_flash_write(uint32_t addr, const void *p, uint32_t len)
{
    // 查找ota空闲分区
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    // 如果分区不存在，返回错误
    if (!partition)
    {
        ESP_LOGE("bs_flash_write", "Failed to find partition");
        return 1;
    }

    ESP_LOGI("WRITE_NEW_BIN", "write_addr : 0x%x, write_data_len : 0x%x\n", addr, len);
    addr -= partition->address;

    esp_err_t err = esp_partition_write(partition, addr, p, len);
    if (err != ESP_OK)
    {
        ESP_LOGE("bs_flash_write", "Partition write failed: %s", esp_err_to_name(err));
        return 1;
    }

    return 0;
}

void diff_OTA(char *diff_partition_name)
{
    /* 1.通过内存映射的方式使程序能够安全的访问运行中分区的内容 */
    ESP_LOGI(TAG, "1.通过内存映射的方式使程序能够安全的访问运行中分区的内容");
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL)
    {
        ESP_LOGE(TAG, "Unable to find running partition");
        return;
    }

    spi_flash_mmap_handle_t mmap_handle_running;
    const void *mapped_memory;
    esp_err_t err = spi_flash_mmap(running_partition->address, running_partition->size, SPI_FLASH_MMAP_DATA, &mapped_memory, &mmap_handle_running);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_flash_mmap failed: %s", esp_err_to_name(err));
        spi_flash_munmap(mmap_handle_running);
        return;
    }
    /* 现在可以通过 mapped_memory 指针安全地访问运行分区的内容 */
    const uint8_t *data = (const uint8_t *)mapped_memory;

    /* 2.获取并清除另一OTA分区中的内容 */
    ESP_LOGI(TAG, "2.获取并清除另一OTA分区中的内容");
    const esp_partition_t *next_update_partition = esp_ota_get_next_update_partition(NULL);
    if (next_update_partition == NULL)
    {
        ESP_LOGE(TAG, "Unable to find update partition");
        spi_flash_munmap(mmap_handle_running);
        return;
    }
    err = esp_partition_erase_range(next_update_partition, 0, next_update_partition->size);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Partition erase failed: %s", esp_err_to_name(err));
        spi_flash_munmap(mmap_handle_running);
        return;
    }

    /* 3.获取差分文件区中差分文件的内容（此步之前须通过url下载差分文件并存储到对应的差分区） */
    ESP_LOGI(TAG, "3.获取差分文件区中差分文件的内容(此步之前须通过url下载差分文件并存储到对应的差分区)");
    const esp_partition_t *diff_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, diff_partition_name);
    if (!diff_partition)
    {
        ESP_LOGE(TAG, "diff_partition is NULL");
        spi_flash_munmap(mmap_handle_running);
        return;
    }
    /* 使用 esp_partition_mmap 映射 diff_partition */
    spi_flash_mmap_handle_t mmap_handle_next;
    const void *mapped_ptr;
    esp_err_t ret = esp_partition_mmap(diff_partition, 0, diff_partition->size, SPI_FLASH_MMAP_DATA, &mapped_ptr, &mmap_handle_next);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_partition_mmap failed: %s", esp_err_to_name(ret));
        spi_flash_munmap(mmap_handle_running);
        spi_flash_munmap(mmap_handle_next);
        return;
    }
    /* 现在可以通过 mapped_memory 指针安全地访问差分分区的内容 */
    const uint8_t *diff_package = (const uint8_t *)mapped_ptr;

    /* 解析差分包文件头 */
    const image_header_t *diff_header = (const image_header_t *)diff_package;

    /* 从差分包头中获取旧固件大小、新固件大小和差分包大小 */
    uint32_t diff_package_size = BigtoLittle32(diff_header->ih_size); /*!< 差分包的大小(数据由大端模式转换为小端模式) */
    uint32_t old_firmware_size = diff_header->ih_load;                /*!< 上一版本旧文件的大小 */
    uint32_t new_firmware_size = diff_header->ih_ep;                  /*!< 要升级的新文件的大小 */
    ESP_LOGI(TAG, "差分包的大小:%d\n", diff_package_size);
    ESP_LOGI(TAG, "上一版本旧文件的大小:%d\n", old_firmware_size);
    ESP_LOGI(TAG, "要升级的新文件的大小:%d\n\n", new_firmware_size);

    image_header_t recv_head;  /*!< 接收文件头 */
    uint32_t recv_hcrc;        /*!< 接收到的文件头CRC */
    uint32_t calculation_crc;  /*!< 计算出来的文件头CRC */

    memcpy(&recv_head, (uint8_t *)diff_header, sizeof(image_header_t));
    recv_hcrc = BigtoLittle32(recv_head.ih_hcrc);
    recv_head.ih_hcrc = 0;
    calculation_crc = crc32((uint8_t *)&recv_head, sizeof(image_header_t));

    /* 4.验证文件头CRC */
    ESP_LOGI(TAG, "4.验证文件头CRC");
    if (recv_hcrc == calculation_crc)
    {
        recv_head.ih_hcrc = recv_hcrc;
        recv_head.ih_time = BigtoLittle32(recv_head.ih_time);
        recv_head.ih_size = BigtoLittle32(recv_head.ih_size);
        recv_head.ih_dcrc = BigtoLittle32(recv_head.ih_dcrc);
        recv_head.ih_ocrc = BigtoLittle32(recv_head.ih_ocrc);

        /* 5.验证旧版本固件CRC */
        ESP_LOGI(TAG, "5.验证旧版本固件CRC");
        recv_head.ih_hcrc = calculation_crc;
        if (crc32(data, recv_head.ih_load) != recv_head.ih_ocrc)
        {
            ESP_LOGE(TAG, "file oldcrc err,calcrc:0X%08x, ih_oldbin_crc:0X%08x\n",
                    crc32(data, recv_head.ih_load),
                    recv_head.ih_ocrc);
            spi_flash_munmap(mmap_handle_running);
            spi_flash_munmap(mmap_handle_next);
            return;
        }

        /* 6.解析差分文件，将新版本固件写入OTA分区 */
        ESP_LOGI(TAG, "6.解析差分文件,将新版本固件写入OTA分区");
        new_firmware_size = iap_patch(data, old_firmware_size,
                                      (uint8_t *)(diff_package + sizeof(image_header_t)),
                                      diff_package_size, next_update_partition->address);

        /* 7.验证写入文件大小 */
        ESP_LOGI(TAG, "7.验证写入文件大小");
        if (new_firmware_size != recv_head.ih_ep)
        {
            ESP_LOGE(TAG, "iap_patch len err.");
            ESP_LOGE(TAG, "iap_patch len: %d, new_len: %d", new_firmware_size, recv_head.ih_ep);
            spi_flash_munmap(mmap_handle_running);
            spi_flash_munmap(mmap_handle_next);
            return;
        }

        /* 8.设置boot到新的固件分区 */
        ESP_LOGI(TAG, "8.设置boot到新的固件分区");
        if (esp_ota_set_boot_partition(next_update_partition) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set boot partition");
            spi_flash_munmap(mmap_handle_running);
            spi_flash_munmap(mmap_handle_next);
            return;
        }
    }
    else
    {
        ESP_LOGE(TAG, "文件头CRC校验失败");
        spi_flash_munmap(mmap_handle_running);
        spi_flash_munmap(mmap_handle_next);
        return;
    }

    /* 9.释放数据并重启 */
    ESP_LOGI(TAG, "9.释放数据并重启");
    spi_flash_munmap(mmap_handle_running);
    spi_flash_munmap(mmap_handle_next);
    esp_restart();
}
