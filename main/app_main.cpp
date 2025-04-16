#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include "freertos/FreeRTOS.h" // Added for vTaskDelay
#include "freertos/task.h"     // Added for vTaskDelay

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <common_macros.h>
#include <app_priv.h>
#include <app_reset.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
#include <esp_matter_providers.h>
#include <lib/support/Span.h>
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
#include <platform/ESP32/ESP32SecureCertDACProvider.h>
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
#include <platform/ESP32/ESP32FactoryDataProvider.h>
#endif
using namespace chip::DeviceLayer;
#endif

#include "led_strip_control.h"
#include "web_server.h"
#include "FFT.h"
#include "weather.h"
#include "display.h"

static const char *TAG = "app_main";
uint16_t light_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
extern const uint8_t cd_start[] asm("_binary_certification_declaration_der_start");
extern const uint8_t cd_end[] asm("_binary_certification_declaration_der_end");

const chip::ByteSpan cdSpan(cd_start, static_cast<size_t>(cd_end - cd_start));
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

#if CONFIG_ENABLE_ENCRYPTED_OTA
extern const char decryption_key_start[] asm("_binary_esp_image_encryption_key_pem_start");
extern const char decryption_key_end[] asm("_binary_esp_image_encryption_key_pem_end");

static const char *s_decryption_key = decryption_key_start;
static const uint16_t s_decryption_key_len = decryption_key_end - decryption_key_start;
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        {
            ESP_LOGI(TAG, "Fabric removed successfully");
            if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0)
            {
                chip::CommissioningWindowManager & commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
                constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
                if (!commissionMgr.IsCommissioningWindowOpen())
                {
                    /* After removing last fabric, this example does not remove the Wi-Fi credentials
                     * and still has IP connectivity so, only advertising on DNS-SD.
                     */
                    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds,
                                                    chip::CommissioningWindowAdvertisement::kDnssdOnly);
                    if (err != CHIP_NO_ERROR)
                    {
                        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
                    }
                }
            }
        break;
        }

    case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
        ESP_LOGI(TAG, "Fabric will be removed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
        ESP_LOGI(TAG, "Fabric is updated");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
        ESP_LOGI(TAG, "Fabric is committed");
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;

    default:
        break;
    }
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    esp_err_t err = ESP_OK;

    if (type == PRE_UPDATE) {
        /* Driver update */
        app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
        err = app_driver_attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
    }

    return err;
}

// Function to print WiFi MAC address
static void print_wifi_mac(void)
{
    uint8_t mac[6];
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi MAC Address: %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ESP_LOGI(TAG, "mDNS Address: %02X%02X%02X%02X%02X%02X.local",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGE(TAG, "Failed to get WiFi MAC address: %s", esp_err_to_name(err));
    }
}

// Task to periodically run FFT processing when adaptive mode is active
static void adaptive_mode_task(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(500); // Adaptive mode sample frequency

    while (1) {
        // If in adaptive mode and powered on, run the FFT control
        if (led_strip_get_mode() == MODE_ADAPTIVE && led_strip_get_power_state()) {
            // This function should set individual pixel colors via led_strip_set_pixel_color()
            fft_control_lights();

            // Explicitly call led_strip_update() AFTER fft_control_lights() sets the pixels.
            // led_strip_update() will check the mode/power again and call led_strip_refresh().
            esp_err_t update_err = led_strip_update();
            if (update_err != ESP_OK) {
                ESP_LOGE(TAG, "Adaptive task: Failed to update LED strip: %s", esp_err_to_name(update_err));
            }
        }

        // Delay until next cycle
        vTaskDelayUntil(&last_wake_time, frequency);
    }
}

// Task to periodically update lighting based on environmental conditions
static void environmental_mode_task(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    // Fetch weather and update target color every 15 minutes
    const TickType_t frequency = pdMS_TO_TICKS(15 * 60 * 1000);

    while (1) {
        ESP_LOGI(TAG, "Environmental task: Triggering weather fetch/cache update.");
        esp_err_t weather_err = fetch_and_update_weather_state();
        if (weather_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to fetch or update weather state: %s", esp_err_to_name(weather_err));
            // Continue anyway, will use previously cached data
        }

        // Get the latest cached weather data (might be old if fetch failed)
        double temp = weather_get_cached_temp();
        int condition_id = weather_get_cached_condition_id();
        const char* condition_desc = weather_get_cached_condition_desc();

        ESP_LOGD(TAG, "Environmental task: Updating target environmental color based on cached state (Temp=%.1f, ID=%d, Desc=%s)",
                 temp, condition_id, condition_desc);

        // Update the target environmental RGB values stored in led_strip_control
        esp_err_t update_target_err = led_strip_update_environmental_state(temp, condition_id, condition_desc);
        if (update_target_err != ESP_OK) {
             ESP_LOGE(TAG, "Failed to update target environmental state: %s", esp_err_to_name(update_target_err));
             // Log error, but continue the loop
        }

        // If the strip is currently in environmental mode, apply the updated target color
        if (led_strip_get_mode() == MODE_ENVIRONMENTAL) {
            ESP_LOGI(TAG, "Environmental task: Mode is ENV, triggering strip update.");
            // Call the main update function which will use the new environmental_r/g/b values
            esp_err_t apply_err = update_led_strip();
            if (apply_err != ESP_OK) {
                ESP_LOGE(TAG, "Environmental task: Failed to apply updated state to strip: %s", esp_err_to_name(apply_err));
            }
        } else {
            ESP_LOGD(TAG, "Environmental task: Mode is not ENV, skipping strip update.");
        }

        // Wait until the next 15-minute interval
        vTaskDelayUntil(&last_wake_time, frequency);
    }
}

// Task to periodically update the display
static void display_update_task(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(1000); // Update display every 1 second

    while (1) {
        esp_err_t err = update_display();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update display: %s", esp_err_to_name(err));
        }
        // Delay until next cycle
        vTaskDelayUntil(&last_wake_time, frequency);
    }
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    /* Initialize driver */
    app_driver_handle_t light_handle = app_driver_light_init();
    app_driver_handle_t button_handle = app_driver_button_init();
    app_reset_button_register(button_handle);

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;

    // node handle can be used to add/modify other endpoints.
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    extended_color_light::config_t light_config;
    light_config.on_off.on_off = DEFAULT_POWER;
    light_config.on_off.lighting.start_up_on_off = nullptr;
    light_config.level_control.current_level = DEFAULT_BRIGHTNESS;
    light_config.level_control.on_level = DEFAULT_BRIGHTNESS;
    light_config.level_control.lighting.start_up_current_level = DEFAULT_BRIGHTNESS;
    light_config.color_control.color_mode = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;
    light_config.color_control.enhanced_color_mode = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;
    light_config.color_control.color_temperature.startup_color_temperature_mireds = nullptr;

    // endpoint handles can be used to add/modify clusters.
    endpoint_t *endpoint = extended_color_light::create(node, &light_config, ENDPOINT_FLAG_NONE, light_handle);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create extended color light endpoint"));

    /* Enable HSL control */
    cluster_t *cluster = cluster::get(endpoint, ColorControl::Id);
    cluster::color_control::feature::hue_saturation::config_t hue_saturation_config;
    hue_saturation_config.current_hue = DEFAULT_HUE;
    hue_saturation_config.current_saturation = DEFAULT_SATURATION;
    cluster::color_control::feature::hue_saturation::add(cluster, &hue_saturation_config);

    light_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Light created with endpoint_id %d", light_endpoint_id);

    /* Mark deferred persistence for some attributes that might be changed rapidly */
    attribute_t *current_level_attribute = attribute::get(light_endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    attribute::set_deferred_persistence(current_level_attribute);

    attribute_t *current_x_attribute = attribute::get(light_endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentX::Id);
    attribute::set_deferred_persistence(current_x_attribute);
    attribute_t *current_y_attribute = attribute::get(light_endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentY::Id);
    attribute::set_deferred_persistence(current_y_attribute);
    attribute_t *color_temp_attribute = attribute::get(light_endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
    attribute::set_deferred_persistence(color_temp_attribute);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD && CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION
    // Enable secondary network interface
    secondary_network_interface::config_t secondary_network_interface_config;
    endpoint = endpoint::secondary_network_interface::create(node, &secondary_network_interface_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create secondary network interface endpoint"));
#endif


#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
    auto * dac_provider = get_dac_provider();
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
    static_cast<ESP32SecureCertDACProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
    static_cast<ESP32FactoryDataProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#endif
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    /* Print WiFi MAC address */
    print_wifi_mac();

    /* Starting driver with default values */
    app_driver_light_set_defaults(light_endpoint_id);

#if CONFIG_ENABLE_ENCRYPTED_OTA
    err = esp_matter_ota_requestor_encrypted_init(s_decryption_key, s_decryption_key_len);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to initialized the encrypted OTA, err: %d", err));
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
#if CONFIG_OPENTHREAD_CLI
    esp_matter::console::otcli_register_commands();
#endif
    esp_matter::console::init();
#endif

    // Initialize and start the web server after Matter is configured
    web_server_init();
    web_server_start();

    /* Initialize Weather Module */
    weather_init(); // Added here

    // --- Initial Weather Fetch and Target Color Update ---
    ESP_LOGI(TAG, "Waiting briefly before initial weather fetch...");
    vTaskDelay(pdMS_TO_TICKS(10000)); // Wait 10 seconds for WiFi connection (adjust as needed)
    ESP_LOGI(TAG, "Performing initial weather fetch...");
    err = fetch_and_update_weather_state(); // Use the err declared at the start of app_main
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Initial weather fetch failed: %s. Using default target color.", esp_err_to_name(err));
        // Target color defaults to blueish, which is fine as a fallback
    } else {
        ESP_LOGI(TAG, "Initial weather fetch successful. Updating initial target environmental color.");
        // Update the target environmental color based on the initial fetch
        double temp = weather_get_cached_temp();
        int condition_id = weather_get_cached_condition_id();
        const char* condition_desc = weather_get_cached_condition_desc();
        esp_err_t target_err = led_strip_update_environmental_state(temp, condition_id, condition_desc);
        if (target_err != ESP_OK) {
             ESP_LOGE(TAG, "Failed to set initial target environmental state: %s", esp_err_to_name(target_err));
        }
    }
    // --- End Initial Weather Fetch ---

    if (!initialize_fft()) {
        ESP_LOGE(TAG, "FFT initialization failed");
        return;
    } else {
        // Create task for adaptive mode FFT processing
        xTaskCreate(adaptive_mode_task, "adaptive_mode_task", 4096, NULL, 5, NULL);

    }

    // Create task for environmental mode processing
    xTaskCreate(environmental_mode_task, "environmental_mode_task", 8192, NULL, 4, NULL); // Lower priority than adaptive
        
    ESP_LOGI(TAG, "Web server initialized and started");

    // --- Initialize Display ---
    ESP_LOGI(TAG, "Initializing Display...");
    err = init_display();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Display initialized successfully. Starting display tasks...");
        // Create task for display updates
        xTaskCreate(display_update_task, "display_update_task", 4096, NULL, 3, NULL); // Adjust stack size and priority as needed
        ESP_LOGI(TAG, "Display update task started.");
    }
    // --- End Display Initialization ---
}
