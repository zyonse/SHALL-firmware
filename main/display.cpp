extern "C" {
    #include "display.h"
    }
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

//lv_obj_t* label = (lv_obj_t*)lv_timer_get_user_data(timer);

static inline lv_color_t lv_color_green() {
    return lv_color_make(0x00, 0xFF, 0x00);
}

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
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"


#include <time.h> // time and ntp library
#include "esp_sntp.h"
#include "esp_task_wdt.h"

// WiFi - SSID + Passwords
// #define WIFI_SSID      "iPhoneG"    // phone hotspot
// #define WIFI_PASSWORD  "test12345"

#define WIFI_SSID      "TP-Link_3990"    // phone hotspot
#define WIFI_PASSWORD  "50309856"

// #define WIFI_SSID      "daboyz"    // wifi
// #define WIFI_PASSWORD  "yitbosEK22"

// Global variables for UI updates //
lv_obj_t* wifi_status_label = NULL;
lv_obj_t* time_label = NULL;

// Global variables to store new values and update flags //
volatile bool wifi_status_updated = false;
char global_wifi_status[64] = "";

volatile bool time_updated = false;
char global_time_str[64] = "";
//-----------------------------------------------------------------------
// Spotify Creds


#include "esp_http_client.h"
#include <cJSON.h>
lv_obj_t* spotify_label = NULL;
//#include "ip_config.h"  // defines BACKEND_IP

//-----------------------------------------------------------------------

//--- Function Prototypes --- TODO : Make a header file
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data);
void sync_time_with_ntp(void);
void update_time_label(lv_timer_t *timer);
void init_wifi(void);
//void init_display(void);
//-----------------------------------------------------------------------

/**  
 * @brief Update the time label continuously 
 */
//void update_time_label(lv_task_t *task) 
void update_time_label(lv_timer_t *timer) {
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[128];
    //strftime(buf, sizeof(buf), "%A, %B %d, %Y %I:%M:%S %p", &timeinfo);
    strftime(buf, sizeof(buf), "%A, %B %d, %Y %I:%M:%S%p", &timeinfo);

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
 * @brief Initializes NVS, TCP/IP stack, and WiFi in station mode.
 * Function initializes the NVS, TCP/IP stack and event loop,
 * configures WiFi (including event handler registration), and starts WiFi in STA mode.
 */
void init_wifi(void){
    // Initialize NVS
    esp_err_t ret_nvs = nvs_flash_init();
    if (ret_nvs == ESP_ERR_NVS_NO_FREE_PAGES || ret_nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret_nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret_nvs);

    // TCP/IP + Wi-Fi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // create time label (initially placeholder)
    // time_label = lv_label_create(lv_scr_act(), NULL);
    // lv_label_set_text(time_label, "Time: --:--:--");
    // lv_obj_set_style_local_text_color(time_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    // lv_obj_set_style_local_text_font(time_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_22);
    //lv_obj_align(time_label, NULL, LV_ALIGN_CENTER, 0, 0);

    // create wifi status label on screen
    // wifi_status_label = lv_label_create(lv_scr_act(), NULL);
    // lv_label_set_text(wifi_status_label, "Connecting to Wi-Fi...");
    // lv_obj_align(wifi_status_label, NULL, LV_ALIGN_IN_TOP_MID, 0, 10);

    // START WIFI
    ESP_ERROR_CHECK(esp_wifi_start());
}

/**
 * @brief WiFi and IP event handler.
 *
 * This function processes WiFi-related events:
 * - Start: WIFI_EVENT_STA_START -> initiates connection & updates status label.
 * - Disconnects: WIFI_EVENT_STA_DISCONNECTED -> attempts to reconnect and updates the label accordingly.
 * - Got IP address: IP_EVENT_STA_GOT_IP -> logs the IP, updates the status label, stores the global WiFi status, 
 *   and triggers SNTP synchronization.
 *
 * @param arg User-defined argument (unused).
 * @param event_base The event base, e.g., WIFI_EVENT or IP_EVENT.
 * @param event_id The specific event identifier.
 * @param event_data Pointer to event-specific data.
 * 
 * @note Function is adapted from https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/system_time.html
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    (void) arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI("WiFi", "Disconnected! Reconnecting...");
        esp_wifi_connect();
    } 

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI("WiFi", "Connected! Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        // Update WiFi status globals
        snprintf(global_wifi_status, sizeof(global_wifi_status), "Connected!\nIP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_status_updated = true;
        // Synchronize time via SNTP (this is blocking; consider moving to a separate task if needed)
        esp_log_level_set("sntp", ESP_LOG_DEBUG);
        sync_time_with_ntp();
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
        .pclk_hz = 40 * 1000 * 1000, // clock speed
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,  
        .lcd_param_bits = 8, 
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

    vTaskDelay(pdMS_TO_TICKS(10)); // addded

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

    vTaskDelay(pdMS_TO_TICKS(10)); // addded

    // Reset & Initialize TFT Display
    vTaskDelay(pdMS_TO_TICKS(10)); // addded
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_disp_on_off(panel, true);
    ESP_LOGI(TAG, "Display Reset and Turned On!");

    // Initialize LVGL
    lv_init();

    // DISPLAY SETUP: LVGL Display Buffer & Driver ---
    // display buffer
    // static lv_color_t buf1[240 * 40];
    // static lv_color_t buf2[240 * 40];
    // Allocate LVGL buffers using DMA-capable memory
    static lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * 240 * 40, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    static lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * 240 * 40, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    // Number of pixels in each buffer
    const uint32_t buf_pixels = 240 * 40;

    // LVGL rendering pixels and send to display
    lv_display_t *disp = lv_display_create(240, 320);
    lv_display_set_buffers(disp, buf1, NULL, buf_pixels, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);

    // Physical hardware adjustment
    esp_lcd_panel_swap_xy(panel, true);
    esp_lcd_panel_mirror(panel, true, false);

    //lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    esp_task_wdt_reset();  // Before
    //lv_display_set_buffers(disp, buf1, buf2, sizeof(lv_color_t) * 240 * 40, LV_DISPLAY_RENDER_MODE_PARTIAL);
    esp_task_wdt_reset();  // After
    vTaskDelay(pdMS_TO_TICKS(10));

    lv_display_set_flush_cb(disp, [](lv_display_t * disp, const lv_area_t * area, uint8_t * color_p) {
        if (panel && area && color_p) {
            esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, (lv_color_t *)color_p);         
        }
        lv_display_flush_ready(disp);
    });

    // mirroring control
    //lv_display_set_rotation(disp, LV_DISP_ROTATION_270);

    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);

    // Physical hardware adjustment
    esp_lcd_panel_swap_xy(panel, true);
    esp_lcd_panel_mirror(panel, false, false);
    //esp_lcd_panel_mirror(panel, false, true);
    ESP_LOGI(TAG, "LVGL Initialized and Display Registered.");

    // LVGL screen
    // lv_obj_t * screen = lv_obj_create(NULL, NULL);  
    // lv_scr_load(screen);
    // lv_obj_set_style_local_bg_color(lv_scr_act(), LV_PART_MAIN, LV_STATE_DEFAULT, lv_color_black());
    // lv_obj_set_style_local_text_color(screen, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
}

void start_and_update_spotify(lv_timer_t *timer) {
    lv_obj_t* label = (lv_obj_t*)lv_timer_get_user_data(timer);

    esp_http_client_config_t config = {
        .url = "http://127.0.0.1:8888/current-track",
        //.url = BACKEND_IP,
        .method = HTTP_METHOD_GET,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_LOGI("SPOTIFY", "Performing HTTP GET to %s", config.url);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI("SPOTIFY", "HTTP status code: %d", status);

        const int buf_size = 256;
        char buffer[buf_size];
        int read_len = esp_http_client_read_response(client, buffer, buf_size - 1);

        if (read_len >= 0) {
            buffer[read_len] = '\0';  // Null terminate
            ESP_LOGI("SPOTIFY", "Response content: %s", buffer);

            cJSON* json = cJSON_Parse(buffer);
            if (json) {
                const cJSON* song = cJSON_GetObjectItem(json, "song");
                const cJSON* artist = cJSON_GetObjectItem(json, "artist");

                if (cJSON_IsString(song) && cJSON_IsString(artist)) {
                    char output[128];
                    snprintf(output, sizeof(output), "Spotify: %s - %s", song->valuestring, artist->valuestring);
                    lv_label_set_text(label, output);
                } else {
                    ESP_LOGE("SPOTIFY", "JSON missing song or artist");
                }

                cJSON_Delete(json);
            } else {
                ESP_LOGE("SPOTIFY", "Failed to parse JSON. Raw: %s", buffer);
            }
        } else {
            ESP_LOGE("SPOTIFY", "Failed to read HTTP response");
        }
    } else {
        ESP_LOGE("SPOTIFY", "HTTP request failed: %s", esp_err_to_name(err));
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
        lv_tick_inc(5); // increment LVGL tick 1ms
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// MAIN FUNCTION
void start_lvgl_app(void *pvParameters) {
    ESP_LOGI("LVGL", "LVGL version: %d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    

    esp_task_wdt_add(NULL); // Register current task to feed watchdog

    // start the LVGL tick task to increment LVGL's internal tick count.
    xTaskCreate(lv_tick_task, "lv_tick_task", 1024, NULL, 1, NULL);

    // Initialize display and WiFi
    init_display();
    vTaskDelay(pdMS_TO_TICKS(100));  // Yield briefly
    esp_task_wdt_reset();            // Reset the watchdog
    //init_wifi();     

    // set time
    setenv("TZ", "EST5EDT", 1);
    tzset();

    // Create a new screen and load it so that it becomes active
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_scr_load(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);

    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE); // ensure no weird scroll artifacts
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);

    // Create the time label on the active screen
    time_label = lv_label_create(screen);
    lv_label_set_text(time_label, "Hello World");
    // lv_label_set_long_mode(time_label, LV_LABEL_LONG_CLIP);
    // lv_obj_set_width(time_label, 400);
    // lv_obj_set_style_text_color(time_label, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN); // Use LVGL predefined green // Explicit green
    // lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14, LV_PART_MAIN);
    // lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, 0, 0);

    // Spotify Label
    // spotify_label = lv_label_create(screen);
    // lv_label_set_text(spotify_label, "Spotify: --");
    // lv_obj_set_style_text_color(spotify_label, lv_color_green(), LV_PART_MAIN);
    // lv_obj_set_style_text_font(spotify_label, &lv_font_montserrat_14, LV_PART_MAIN);
    // lv_obj_align(spotify_label, LV_ALIGN_LEFT_MID, 0, 20);
    // lv_timer_create(start_and_update_spotify, 10000, spotify_label);

    // Schedule the time update task to run every 1000 ms (1 second).
    //lv_task_t *time_task = lv_task_create(update_time_label, 500, LV_TASK_PRIO_LOW, NULL);
    void update_time_label(lv_timer_t *timer);
    lv_timer_create(update_time_label, 500, NULL);  // call every 500ms
    esp_task_wdt_reset();

    //Main loop: process LVGL tasks.
    while (1) {
        esp_task_wdt_reset();
        lv_timer_handler(); 
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// // MAIN FUNCTION
// extern "C" void app_main() {
//     ESP_LOGI("LVGL", "LVGL version: %d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    
//     // start the LVGL tick task to increment LVGL's internal tick count.
//     xTaskCreate(lv_tick_task, "lv_tick_task", 1024, NULL, 1, NULL);

//     // Initialize display and WiFi
//     init_display();
//     init_wifi();     

//      // set time
//      setenv("TZ", "EST5EDT", 1);
//      tzset();

//     // Create a new screen and load it so that it becomes active
//     lv_obj_t *screen = lv_obj_create(NULL);
//     lv_scr_load(screen);
//     lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);

//     lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE); // ensure no weird scroll artifacts
//     lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);

//     // Create the time label on the active screen
//     time_label = lv_label_create(screen);
//     lv_label_set_text(time_label, "");
//     lv_label_set_long_mode(time_label, LV_LABEL_LONG_CLIP);
//     lv_obj_set_width(time_label, 400);
//     lv_obj_set_style_text_color(time_label, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN); // Use LVGL predefined green // Explicit green
//     lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14, LV_PART_MAIN);
//     lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, 0, 0);

//     // Spotify Label
//     // spotify_label = lv_label_create(screen);
//     // lv_label_set_text(spotify_label, "Spotify: --");
//     // lv_obj_set_style_text_color(spotify_label, lv_color_green(), LV_PART_MAIN);
//     // lv_obj_set_style_text_font(spotify_label, &lv_font_montserrat_14, LV_PART_MAIN);
//     // lv_obj_align(spotify_label, LV_ALIGN_LEFT_MID, 0, 20);
//     // lv_timer_create(start_and_update_spotify, 10000, spotify_label);

//     // Schedule the time update task to run every 1000 ms (1 second).
//     //lv_task_t *time_task = lv_task_create(update_time_label, 500, LV_TASK_PRIO_LOW, NULL);
//     void update_time_label(lv_timer_t *timer);
//     lv_timer_create(update_time_label, 500, NULL);  // call every 500ms

//     // Main loop: process LVGL tasks.
//     while (1) {
//         lv_timer_handler(); 
//         vTaskDelay(pdMS_TO_TICKS(100));
//     }
// }