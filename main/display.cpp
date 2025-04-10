//--- DISPLAY LIBRARIES, CONNECTIONS, GLOBAL VARIBALES, EXAMPLE OF LABEL
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// LVGL (graphically library for TFT)
#include "lvgl.h"
#include "lvgl_helpers.h"

// TFT DISPLAY PIN -> ESP32 PIN NUMBER
#define TFT_MOSI  11
#define TFT_CLK   12 // SCK
#define TFT_CS    10
#define TFT_DC    8
#define TFT_RST   9

// connected to 3.3v dont need
//#define TFT_BCKL  -1 

// GLOBAL DISPLAY VARIABLES
static const char *TAG = "TFT Display";
esp_lcd_panel_handle_t panel = NULL;
esp_lcd_panel_io_handle_t io_handle;

/*    // // CREATE LABEL EXAMPLE OF HELLO WORLD
    lv_obj_t * label = lv_label_create(screen, NULL);
    lv_label_set_text(label, "Hello World!");
    lv_obj_align(label, NULL, LV_ALIGN_IN_LEFT_MID, 0, 0);
    ESP_LOGI(TAG, "Displayed 'Hello, World!'");
    lv_disp_set_rotation(NULL, LV_DISP_ROT_90); 
    esp_lcd_panel_mirror(panel, false, true);   // needs to mirror the y-axis 
*/
//-----------------------------------------------------------------------

//--- WIFI LIBRARIES, SSID & PASSWORDS
#include "esp_event.h"
#include "nvs_flash.h"


#include <time.h> // time and ntp library
#include "esp_sntp.h"

// Global variables for UI updates //
lv_obj_t* wifi_status_label = NULL;
lv_obj_t* time_label = NULL;

// Global variables to store new values and update flags //
volatile bool wifi_status_updated = false;
char global_wifi_status[64] = "";

volatile bool time_updated = false;
char global_time_str[64] = "";
//-----------------------------------------------------------------------

#include "esp_http_client.h"
#include <cJSON.h>
lv_obj_t* spotify_label = NULL;

//-----------------------------------------------------------------------

//--- Function Prototypes --- TODO : Make a header file
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data);
void sync_time_with_ntp(void);
void update_time_label(lv_task_t *task);
void init_display(void);
//-----------------------------------------------------------------------

/**  
 * @brief Update the time label continuously 
 */
void update_time_label(lv_task_t *task) {
    (void)task;
    //ESP_LOGI(TAG, "update_time_label triggered");
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[128];
    //strftime(buf, sizeof(buf), "%I:%M:%S %p", &timeinfo);
    strftime(buf, sizeof(buf), "%A, %B %d, %Y %I:%M:%S %p", &timeinfo);

    //ESP_LOGI(TAG, "Time update task: %s", buf);
    // Update the global time string and flag
    // strncpy(global_time_str, buf, sizeof(global_time_str));
    // time_updated = true;
    if (time_label) {
        lv_label_set_text(time_label, buf);
        //ESP_LOGI(TAG, "Time label content: %s", lv_label_get_text(time_label));
    }
}

/**
 * @brief Synchronizes the system time with an NTP server.
 *
 * This function configs the SNTP client in polling mode & connects to "pool.ntp.org".
 * It then initializes the SNTP service and waits for the time sync
 *
 * @note  Sync finishes on avg in 2–3 attempts.
 */
void sync_time_with_ntp(){
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // wait for time sync
    int retry = 0;
    const int max_retries = 20;
    while (retry < max_retries) {
        int status = esp_sntp_get_sync_status();
        ESP_LOGI("SNTP", "Attempt %d: sync status = %d", retry, status);
        // SNTP_SYNC_STATUS_COMPLETED = 1, and reset/in progress = 0
        if (status == SNTP_SYNC_STATUS_COMPLETED) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));  // wait 1 seconds
        retry++;
    }
}

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
        printf("SPI SUCCUESS");
    }
    esp_log_level_set("spi", ESP_LOG_DEBUG);

    // SETUP LCD SPI INTERFACE CONNECTIONS
    esp_lcd_panel_io_spi_config_t io_config = { 
        .cs_gpio_num = TFT_CS,
        .dc_gpio_num = TFT_DC,
        .spi_mode = 0,        
        .pclk_hz = 1 * 1000 * 1000, // clock speed
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,  
        .lcd_param_bits = 8, 
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .flags = {0}
    };

    esp_err_t ret_2 = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle);
    if (ret_2 != ESP_OK || io_handle == NULL) {
        ESP_LOGE(TAG, "Panel IO Init Failed! Error: %d", ret);
        return;
    }
    else{
        ESP_LOGI(TAG, "ILI9341 driver Initialized");
    }

    // Configure & initialize the ILI9341 driver panel
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TFT_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
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

    // DISPLAY SETUP: LVGL Display Buffer & Driver ---
    // display buffer
    static lv_disp_buf_t disp_buf;
    static lv_color_t buf1[LV_HOR_RES_MAX * 10];  
    lv_disp_buf_init(&disp_buf, buf1, NULL, LV_HOR_RES_MAX * 10);

    // display driver
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.buffer = &disp_buf;

    // LVGL rendering pixels and send to display
    disp_drv.flush_cb = [](lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_p) {
        //ESP_LOGI("LVGL_FLUSH", "Flushing area: x1=%d, y1=%d, x2=%d, y2=%d", area->x1, area->y1, area->x2, area->y2);
        if (panel && area && color_p) {  // Prevent NULL access
            esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
            lv_disp_flush_ready(drv);
        } else {
            ESP_LOGE(TAG, "Invalid panel or buffer pointer!");
        }
    };
    
    // correct rotation setting
    disp_drv.sw_rotate = 1;
    lv_disp_drv_register(&disp_drv);

    // mirroring control
    lv_disp_set_rotation(NULL, LV_DISP_ROT_90);
    esp_lcd_panel_mirror(panel, false, true);
    ESP_LOGI(TAG, "LVGL Initialized and Display Registered.");

    // LVGL screen
    // lv_obj_t * screen = lv_obj_create(NULL, NULL);  
    // lv_scr_load(screen);
    // lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    // lv_obj_set_style_local_text_color(screen, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
}

void start_and_update_spotify(lv_task_t *task) {
    lv_obj_t* label = (lv_obj_t*)task->user_data;

    esp_http_client_config_t config = {
        .url = "http://10.0.0.150:8888/current-track",  // replace with your IP
        .method = HTTP_METHOD_GET,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (esp_http_client_perform(client) == ESP_OK) {
        int len = esp_http_client_get_content_length(client);
        char* buffer = (char*)malloc(len + 1);
        esp_http_client_read_response(client, buffer, len);
        buffer[len] = '\0';

        cJSON* json = cJSON_Parse(buffer);
        if (json) {
            const char* song = cJSON_GetObjectItem(json, "song")->valuestring;
            const char* artist = cJSON_GetObjectItem(json, "artist")->valuestring;
            char output[128];
            snprintf(output, sizeof(output), "Spotify: %s - %s", song, artist);
            lv_label_set_text(label, output);
            cJSON_Delete(json);
        }
        free(buffer);
    }
    esp_http_client_cleanup(client);
}

/**
 * @brief This function
 *
 * @note 
 */
void lv_tick_task(void *arg) {
    (void)arg;
    while (1) {
        lv_tick_inc(900); // increment LVGL tick 1ms
        vTaskDelay(pdMS_TO_TICKS(900));
    }
}

// MAIN FUNCTION
extern "C" void app_main() {
    ESP_LOGI("LVGL", "LVGL version: %d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    
    // start the LVGL tick task to increment LVGL's internal tick count.
    xTaskCreate(lv_tick_task, "lv_tick_task", 1024, NULL, 1, NULL);

    // Initialize display and WiFi
    init_display();

    // Create a new screen and load it so that it becomes active
    lv_obj_t *screen = lv_obj_create(NULL, NULL);
    lv_scr_load(screen);
    lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

    // Create the time label on the active screen
    time_label = lv_label_create(lv_scr_act(), NULL);
    lv_label_set_text(time_label, "");
    lv_label_set_long_mode(time_label, LV_LABEL_LONG_CROP);
    lv_obj_set_width(time_label, 400);
    lv_obj_set_style_local_text_color(time_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_style_local_text_font(time_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_14);
    lv_obj_align(time_label, NULL, LV_ALIGN_IN_TOP_LEFT, 0, 0);

    // Spotify Label
    spotify_label = lv_label_create(lv_scr_act(), NULL);
    lv_label_set_text(spotify_label, "Spotify: --");
    lv_obj_set_style_local_text_color(spotify_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GREEN);
    lv_obj_set_style_local_text_font(spotify_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_14);
    lv_obj_align(spotify_label, NULL, LV_ALIGN_IN_LEFT_MID, 0, 20);  // 20px below time

    // set time
    setenv("TZ", "EST5EDT", 1);
    tzset();

    // Schedule the time update task to run every 1000 ms (1 second).
    lv_task_t *time_task = lv_task_create(update_time_label, 500, LV_TASK_PRIO_LOW, NULL);
    ESP_LOGI(TAG, "Time update task created.");
    lv_task_create(start_and_update_spotify, 10000, LV_TASK_PRIO_LOW, spotify_label);
    ESP_LOGI(TAG, "Created and updated Spotify");

    // Main loop: process LVGL tasks.
    while (1) {
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}