/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"


#include "lwip/err.h"
#include "lwip/sys.h"
#include "http_server_app.h"
#include "input_iot.h"
#include "output_iot.h"
#include "dht11.h"
#include "ledc_app.h"

#include "ws2812b.h"
#include <string.h>
#include "ultrasonic.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      "P812"
#define EXAMPLE_ESP_WIFI_PASS      "1@234567890"
#define EXAMPLE_ESP_MAXIMUM_RETRY  5
static struct dht11_reading dht11_last_data, dht11_current_data;
// static int air = 5;
static char capture_time[30]; 
static int duty;
static uint32_t distance;

#define BLINK_GPIO 2
#define BIT_SHORT_PRESS	    ( 1 << 0 )
#define BIT_NORMAL_PRESS	( 1 << 1 )
#define BIT_LONG_PRESS	    ( 1 << 2 )
static EventGroupHandle_t xCreatedEventGroup;

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_DISTANCE_CM 500 // 5m max

static const char *TAG = "wifi station";

static int s_retry_num = 0;
static int flag = 0;


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
	     .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

void switch_data_callback(char *data, int length)
{
    if (*data == '1')
    {
        output_io_set(5,1);
    }else if (*data == '0')
    {
        output_io_set(5,0);
    }
}

void voice_data_callback(char *data, int length)
{
   if (*data == '1')
    {
        printf(" Led is on by your order\n");
        output_io_set(5,1);
    }else if (*data == '0')
    {
        output_io_set(5,0);
        printf(" Led is off by your order\n");
    }else if(*data == '2')
    {
        flag = 1;
    }else if(*data == '3')
    {
        printf("The temperature has been decreased by your order\n");
        duty = duty+20;
        printf("Level of Air Conditioning : %d\t", duty);
        LedC_Set_Duty(0,duty);
    }else if(*data == '4')
    {
        printf("The temperature has been increased by your order\n");
        duty = duty-20;
        printf("Level of Air Conditioning : %d\t", duty);
        LedC_Set_Duty(0,duty);
    }else if(*data == '5')
    {
        printf (" Captured time : %s \t",capture_time);

    }else if(*data == '6')
    {
         printf(" Speed up by your order\n");
        duty = duty+20;
        if(duty > 100)
        duty = 100;
        printf("Level of Air Conditioning : %d\t", duty);
        LedC_Set_Duty(0,duty);
    }
    else if(*data == '7')
    {
         printf(" Speed down by your order\n");
        duty = duty-20;
        if(duty < 0)
        duty = 0;
        printf("Level of Air Conditioning : %d\t", duty);
        LedC_Set_Duty(0,duty);
    }
}   

void fanspeed_data_callback(char *data, int length)
{
  char number_str[10];
  memcpy(number_str, data , length+1);
  printf(" FAN Speed : %s \n", number_str);
  duty = atoi(number_str);
  LedC_Set_Duty(0,duty);
}

void time_data_callback(char *data, int length)
{
  if(flag == 1){
    printf ( "Time is : %s \t \n : " , data);
    flag = 0;
  }
}

void capture_data_callback(char *data, int length)
{
    memcpy(capture_time, data , length+1);
    printf("capture time : %s\n", capture_time);
}

void rgb_data_callback(char *data, int length)
{
  printf(" RGB data : %s\n", data);
}

void dht11_data_callback(void)
{
    char resp[100];
    sprintf(resp,"{\"temperature\": \"%.1f\" , \"humidity\": \"%.1f\"}", dht11_last_data.temperature, dht11_last_data.humidity);
    dht11_response(resp,strlen(resp));
}

void input_event_callback(int pin, uint64_t tick)
{
    if (pin ==  GPIO_NUM_0)
    {
        // // static int x;
        // // gpio_set_level(BLINK_GPIO,x);
        // // x = 1- x;
        // output_io_toggle(BLINK_GPIO);
        int press_ms = tick * portTICK_PERIOD_MS;
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        if (press_ms < 500)
        {
            // Press Short
            xEventGroupSetBitsFromISR(xCreatedEventGroup,   BIT_SHORT_PRESS	, &xHigherPriorityTaskWoken );
        }
        else if (press_ms < 1000)
        {
            // Normal Press
            xEventGroupSetBitsFromISR(xCreatedEventGroup,   BIT_NORMAL_PRESS	, &xHigherPriorityTaskWoken );
        }
        else if (press_ms > 1000 && press_ms < 2000) 
        {
            // Long Press
            xEventGroupSetBitsFromISR(xCreatedEventGroup,   BIT_LONG_PRESS	, &xHigherPriorityTaskWoken );
        }
    }
}

void button_timeout_callback(int pin)
{
    if (pin == GPIO_NUM_0){
        printf("Timeout \n");
        output_io_toggle(5);
    }
}

void Task1(void* parameterq)
{
    for (;;)
    {
        EventBits_t uxBits = xEventGroupWaitBits(
            xCreatedEventGroup,   /* The event group being tested. */
            BIT_SHORT_PRESS | BIT_NORMAL_PRESS | BIT_LONG_PRESS, /* The bits within the event group to wait for. */
            pdTRUE,        /* BIT_0 & BIT_4 should be cleared before returning. */
            pdFALSE,       /* Don't wait for both bits, either bit will do. */
            portMAX_DELAY);/* Wait a maximum of 100ms for either bit to be set. */
    
        if(uxBits & BIT_SHORT_PRESS)
        {
            printf(" Short Button press\n");
            // static int x;
            // gpio_set_level(2,x);
            // x = 1- x;
            // output_io_toggle(2);
        }
        if (uxBits & BIT_NORMAL_PRESS)
        {
           printf(" Normal Button press\n");
        }
        if (uxBits & BIT_LONG_PRESS)
        {
            printf(" Long Button press\n");
        }
    }
}
static void ultrasonic_task(void* arg){
    // Initialize sensor
    ultrasonic_sensor_t sensor = {
        .trigger_pin = 22,
        .echo_pin = 21
    };
    
    ultrasonic_init(&sensor);

    while (1) {
        esp_err_t res = ultrasonic_measure_cm(&sensor, MAX_DISTANCE_CM, &distance);
        if (res != ESP_OK) {
            printf("Error: ");
            switch (res) {
                case ESP_ERR_ULTRASONIC_PING:
                    printf("Cannot ping (device is in invalid state)\n");
                    break;
                case ESP_ERR_ULTRASONIC_PING_TIMEOUT:
                    printf("Ping timeout (no device found)\n");
                    break;
                case ESP_ERR_ULTRASONIC_ECHO_TIMEOUT:
                    printf("Echo timeout (i.e. distance too big)\n");
                    break;
                default:
                    printf("%d\n", res);
            }
        } else {
        printf("Distance: %d cm, %.02f m\n", distance, distance / 100.0);
            if(distance<10){
                output_io_set(32,1);
                output_io_set(5,1);
                vTaskDelay(pdMS_TO_TICKS(2000));
                output_io_set(32,0);
                output_io_set(5,0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000)); // 1000 milliseconds delay
    }
}

void ReadDHT11_task(void *arg){
      // Read DHT11 sensor 
    while (1)
    {
        dht11_current_data = DHT11_read();
        if (dht11_current_data.status == 0)// read oke 
        {
            dht11_last_data = dht11_current_data;
            printf("Temp : %.1f",dht11_last_data.temperature);
            printf(", Hum : %.1f \n",dht11_last_data.humidity);
        }else {
            printf("Fail");
        }
        vTaskDelay(5000/portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    http_set_callback_switch(switch_data_callback);
    http_set_callback_dht11(dht11_data_callback);
    http_set_callback_fanspeed(fanspeed_data_callback);
    http_set_callback_voice(voice_data_callback);
    http_set_callback_rgb(rgb_data_callback);
    http_set_callback_time(time_data_callback);
    http_set_callback_capture(capture_data_callback);

    output_io_create(5);
    output_io_create(32);
    DHT11_init(GPIO_NUM_26);
    
    // Ledc init
    LedC_Init();
    LedC_Add_Pin(GPIO_NUM_2,0);

    WS2812B_Init(GPIO_NUM_15,8);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta(); // initialize wifi in station mode 
    start_webserver();

    xCreatedEventGroup = xEventGroupCreate();
    xTaskCreate(Task1," Task1", 1024, NULL, 4, NULL);

    input_io_create(GPIO_NUM_0, ANY_EDLE);
    input_set_callback(input_event_callback);
    input_set_timeout_callback(button_timeout_callback);

    xTaskCreate(ultrasonic_task, "ultrasonic_task", 1024*2, NULL, configMAX_PRIORITIES-3, NULL); 
    xTaskCreate(ReadDHT11_task, "ReadDHT11_task", 1024*2, NULL, configMAX_PRIORITIES-4, NULL);

}


