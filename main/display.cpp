extern "C" {
    #include "display.h"
    }
//--- DISPLAY LIBRARIES, CONNECTIONS, GLOBAL VARIBALES, EXAMPLE OF LABEL
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

// LVGL (graphically library for TFT)
#include "lvgl.h"

// TFT DISPLAY PIN -> ESP32 PIN NUMBER
#define TFT_MOSI  11
#define TFT_CLK   12 // SCK
#define TFT_CS    10
#define TFT_DC    8
#define TFT_RST   9

// GLOBAL DISPLAY VARIABLES
static const char *TAG = "TFT Display";
esp_lcd_panel_handle_t panel = NULL;
esp_lcd_panel_io_handle_t io_handle;

/**
 * @brief Initial the TFT display, SPI bus, and LVGL.
 *
 * This function sets up the SPI bus, LCD SPI interface,
 * initializes the ILI9341 panel, and then LVGL display drivers.
 */
void init_display(void) {
    // SPI BUS CONFIG
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = TFT_MOSI;
    buscfg.sclk_io_num = TFT_CLK;
    buscfg.miso_io_num = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 320 * 240 * 2 + 8;
    buscfg.flags = SPICOMMON_BUSFLAG_MASTER;

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI Bus Init Failed! Error: %d", ret);
        return;
    }
    else{
        ESP_LOGI(TAG, "SPI Bus Initialized");
    }

    // SETUP LCD SPI INTERFACE CONNECTIONS
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = TFT_CS,
        .dc_gpio_num = TFT_DC,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000, // clock speed
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    esp_err_t ret_2 = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle);
    if (ret_2 != ESP_OK || io_handle == NULL) {
        ESP_LOGE(TAG, "Panel IO Init Failed! Error: %d", ret_2);
        return;
    }
    else{
        ESP_LOGI(TAG, "Panel IO Initialized");
    }

    // Configure & initialize the ILI9341 driver panel
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TFT_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
        .flags = {.reset_active_high = 0},
        .vendor_config = NULL,
    };

    esp_err_t ret_3 = esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel);
    if (ret_3 != ESP_OK || panel == NULL) {
        ESP_LOGE(TAG, "ILI9341 Init Failed! Error: %d", ret_3);
        return;
    }else{
        ESP_LOGI(TAG, "ILI9341 driver Configured");
    }

    // Reset & Initialize TFT Display
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_disp_on_off(panel, true);
    ESP_LOGI(TAG, "Display Reset and Turned On!");

    // Initialize LVGL
    lv_init();
    ESP_LOGI(TAG, "LVGL Initialized");

    // DISPLAY SETUP: LVGL Display Buffer & Driver ---
    const uint32_t buf_pixels = 240 * 10; // Smaller buffer (10 lines)
    static lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * buf_pixels, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffer 1");
        return;
    }
    ESP_LOGI(TAG, "LVGL Buffers Allocated (%lu bytes)", sizeof(lv_color_t) * buf_pixels);

    // LVGL rendering pixels and send to display
    lv_display_t *disp = lv_display_create(240, 320); // Width, Height
    if (!disp) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        heap_caps_free(buf1);
        return;
    }
    lv_display_set_buffers(disp, buf1, NULL, buf_pixels, LV_DISPLAY_RENDER_MODE_PARTIAL);
    ESP_LOGI(TAG, "LVGL Display Buffers Set");

    // Physical hardware adjustment - Apply necessary transformations
    esp_lcd_panel_swap_xy(panel, true);
    esp_lcd_panel_mirror(panel, true, false);
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
    ESP_LOGI(TAG, "Display Orientation Configured");

    lv_display_set_flush_cb(disp, [](lv_display_t * disp, const lv_area_t * area, uint8_t * color_p) {
        if (panel && area && color_p) {
            int32_t width = (area->x2 - area->x1 + 1);
            int32_t height = (area->y2 - area->y1 + 1);
            esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x1 + width, area->y1 + height, (void *)color_p);
        }
        lv_display_flush_ready(disp);
    });
    ESP_LOGI(TAG, "LVGL Flush Callback Set");

    ESP_LOGI(TAG, "LVGL Display Initialized and Registered.");
}

/**
 * @brief LVGL Tick Task
 * Increments LVGL's internal tick counter periodically.
 * @note Recommended to call lv_tick_inc every 1-10ms.
 */
void lv_tick_task(void *arg) {
    (void)arg;
    const TickType_t tick_period_ms = 10; // Call lv_tick_inc every 10ms
    while (1) {
        lv_tick_inc(tick_period_ms);
        vTaskDelay(pdMS_TO_TICKS(tick_period_ms));
    }
}

// MAIN FUNCTION for LVGL App Task
void start_lvgl_app(void *pvParameters) {
    ESP_LOGI("LVGL", "LVGL App Task Started");

    esp_task_wdt_add(NULL); // Register current task to feed watchdog
    esp_task_wdt_reset();   // Initial reset

    // Initialize display
    init_display();
    esp_task_wdt_reset(); // Reset after potentially long init

    // Create a new screen and load it so that it becomes active
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    ESP_LOGI(TAG, "LVGL Screen Created");

    // Create the static label on the active screen
    lv_obj_t *static_label = lv_label_create(screen);
    lv_label_set_text(static_label, "SHALL Initializing...");
    lv_obj_set_style_text_color(static_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(static_label, LV_ALIGN_CENTER, 0, 0);
    ESP_LOGI(TAG, "Static Label Created");

    esp_task_wdt_reset(); // Reset before entering loop

    // Main loop: process LVGL tasks.
    while (1) {
        esp_task_wdt_reset(); // Reset watchdog at the start of the loop
        uint32_t time_ms = lv_timer_handler();
        if (time_ms < 5) {
            time_ms = 5;
        } else if (time_ms > 500) {
            time_ms = 500;
        }
        vTaskDelay(pdMS_TO_TICKS(time_ms));
    }
}