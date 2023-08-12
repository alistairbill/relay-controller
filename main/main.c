#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_https_ota.h>
#include <esp_sleep.h>

#if defined(CONFIG_IDF_TARGET_ESP8266)
#include "connect_esp8266.h"
#else
#include "connect_esp32.h"
#endif

#include "common.h"
#include "homie.h"

static const char *TAG = "RELAY";
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef struct {
    uint8_t gpio;
    const char *id;
} relay_t;

static const relay_t relays[] = {
    {.gpio = CONFIG_GPIO_PUMP, .id = "pump"},
    {.gpio = CONFIG_GPIO_LIGHT, .id = "light"},
};

static void msg_handler(const char *subtopic, const char *data)
{
    char buf[HOMIE_MAX_TOPIC_LEN];
    static char command_true[] = "true";
    static char command_false[] = "false";
    int level;

    if (strncmp(command_true, data, sizeof(command_true)) == 0) {
        level = 0;
    } else if (strncmp(command_false, data, sizeof(command_false)) == 0) {
        level = 1;
    } else {
        ESP_LOGE(TAG, "Invalid command");
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(relays); i++) {
        snprintf(buf, sizeof(buf), "%s/on/set", relays[i].id);
        if (strncmp(buf, subtopic, sizeof(buf)) == 0) {
            gpio_set_level(relays[i].gpio, level);
            homie_remove_retained(buf);
            buf[strlen(buf) - 4] = '\0'; // strip "/set"
            homie_publish(buf, QOS_1, RETAINED, data);
            break;
        }
    }
}

static void connected_handler(void)
{
    char subtopic[HOMIE_MAX_TOPIC_LEN];
    size_t len;
    for (size_t i = 0; i < ARRAY_SIZE(relays); i++) {
        len = snprintf(subtopic, sizeof(subtopic), "%s", relays[i].id);
        strncpy(subtopic + len, "/$name", sizeof(subtopic) - len);
        homie_publish(subtopic, QOS_1, RETAINED, relays[i].id);
        strncpy(subtopic + len, "/$type", sizeof(subtopic) - len);
        homie_publish(subtopic, QOS_1, RETAINED, "switch");
        strncpy(subtopic + len, "/$properties", sizeof(subtopic) - len);
        homie_publish(subtopic, QOS_1, RETAINED, "on");
        strncpy(subtopic + len, "/on/$name", sizeof(subtopic) - len);
        homie_publish(subtopic, QOS_1, RETAINED, "On");
        strncpy(subtopic + len, "/on/$settable", sizeof(subtopic) - len);
        homie_publish(subtopic, QOS_1, RETAINED, "true");
        strncpy(subtopic + len, "/on/$datatype", sizeof(subtopic) - len);
        homie_publish(subtopic, QOS_1, RETAINED, "boolean");
        strncpy(subtopic + len, "/on/set", sizeof(subtopic) - len);
        homie_subscribe(subtopic, QOS_1);
    }
}

static void ota_init(void)
{
    esp_http_client_config_t ota_conf = {
        .url = CONFIG_OTA_URL,
    };
    ESP_LOGI(TAG, "checking for OTA update");
    esp_https_ota(&ota_conf);
}

static void reboot_task(void *args)
{
    // Every 30 seconds, wait 30 seconds for MQTT to connect.
    // If not connected, reboot.
    EventBits_t bits;
    do {
        vTaskDelay(30 * 1000 / portTICK_PERIOD_MS);
        bits = xEventGroupWaitBits(homie_event_group, HOMIE_MQTT_CONNECTED_BIT, pdFALSE, pdTRUE, 30 * 1000 / portTICK_PERIOD_MS);
    } while (bits & HOMIE_MQTT_CONNECTED_BIT);
    ESP_LOGI(TAG, "No MQTT connection, rebooting");
    homie_disconnect();
    esp_restart();
    vTaskDelete(NULL);
}

static void gpio_init(void)
{
    static gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1 << CONFIG_GPIO_PUMP) | (1 << CONFIG_GPIO_LIGHT),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    gpio_set_level(CONFIG_GPIO_PUMP, 1);
    gpio_set_level(CONFIG_GPIO_LIGHT, 1);
}

void app_main(void)
{
    gpio_init();
    xTaskCreate(&reboot_task, "reboot_task", configMINIMAL_STACK_SIZE, NULL, 3, NULL);

    static esp_pm_config_esp8266_t pm_config = {
        .light_sleep_enable = true,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    services_event_group = xEventGroupCreate();

    homie_config_t homie_conf = {
        .mqtt_config = {
            .event_handle = NULL,
            .client_id = "relay-controller",
            .username = CONFIG_MQTT_USERNAME,
            .password = CONFIG_MQTT_PASSWORD,
            .uri = CONFIG_MQTT_URI,
            .keepalive = 15,
            .task_stack = configMINIMAL_STACK_SIZE * 10,
            .cert_pem = NULL,
        },
        .device_name = "Relay controller",
        .base_topic = "homie/relay-controller",
        .loop = true,
        .connected_handler = connected_handler,
        .msg_handler = msg_handler,
        .node_list = "pump,light",
        .stats_interval = 60,
    };

    wifi_init();
    WAIT_FOR(WIFI_CONNECTED_BIT);
    homie_init(&homie_conf);

    while (true) {
        ota_init();
        vTaskDelay(60 * 60 * 1000 / portTICK_PERIOD_MS); // 1 hour
    }
}
