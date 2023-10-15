#include <stdio.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include "input_iot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"


input_callback_t input_callback = NULL;
timeoutButton_t timeoutButton_callback = NULL;
static uint64_t _start,_stop, _pressTick;
static TimerHandle_t xTimers;

static void IRAM_ATTR gpio_input_handler (void *arg)
{
    int gpio_num = (uint32_t)arg; 
    uint32_t rtc = xTaskGetTickCountFromISR();
    if (gpio_get_level(gpio_num) == 0) // Start press button
    {
        xTimerStart( xTimers, 0 );
        _start = rtc;
    }else // Stop press button
    {   
        xTimerStop( xTimers, 0 );
        _stop = rtc;
        _pressTick = _stop - _start;
        input_callback(gpio_num, _pressTick);

    }
}
static void vTimerCallback( TimerHandle_t xTimer ) // Day la ham call back tu softtime khong phai ngat nen co the xu li lau o day 
 {
    configASSERT( xTimer );
    uint32_t ID = ( uint32_t ) pvTimerGetTimerID( xTimer );
    if ( ID == 0 )
    {
        timeoutButton_callback(BUTTON0);
        // printf("vTimerCallback \n");
    }
 }

void input_io_create(gpio_num_t gpio_num, interrupt_type_edle_t type)
{
    gpio_pad_select_gpio(gpio_num);
    gpio_set_direction(gpio_num, GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio_num, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(gpio_num,type);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(gpio_num, gpio_input_handler, (void *)gpio_num); 
    // Create Timer  
    xTimers   = xTimerCreate
                   ( /* Just a text name, not used by the RTOS
                     kernel. */
                     "TimerForTimeOut",
                     3000/portTICK_RATE_MS,
                     /* The timers will auto-reload themselves
                     when they expire. */
                     pdFALSE, //xu li 1 lan 
                     /* The ID is used to store a count of the
                     number of times the timer has expired, which
                     is initialised to 0. */
                     ( void * ) 0,
                     /* Each timer calls the same callback when
                     it expires. */
                     vTimerCallback
                   ); 
}

int input_io_get_level(gpio_num_t gpio_num)
{
    return gpio_get_level(gpio_num);
}

void input_set_callback(void* cb)
{
    input_callback = cb;
}

void input_set_timeout_callback(void* cb)
{
    timeoutButton_callback = cb;
}

