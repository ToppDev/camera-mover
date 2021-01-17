#ifndef GPIO_H
#define GPIO_H

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define LED_GPIO GPIO_NUM_2

#define ESP_INTR_FLAG_DEFAULT 0

#define GPIO_STEP GPIO_NUM_4
#define GPIO_DIR GPIO_NUM_0
#define GPIO_OUTPUT_PIN_SEL ((1ULL << GPIO_STEP) | (1ULL << GPIO_DIR))

#define GPIO_BTN_START GPIO_NUM_15
#define GPIO_BTN_END GPIO_NUM_17
#define GPIO_INPUT_PIN_SEL ((1ULL << GPIO_BTN_START) | (1ULL << GPIO_BTN_END))

#define SAFETY_DIST 100
int btn_start_pressed = 0;
int btn_end_pressed = 0;

static xQueueHandle gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void gpio_task(void *arg)
{
    uint32_t io_num;
    for (;;)
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            int gpio_level = gpio_get_level((gpio_num_t)io_num);
            printf("GPIO[%d] intr, val: %d\n", io_num, gpio_level);
            if (gpio_level)
            {
                if (io_num == GPIO_BTN_START)
                {
                    btn_start_pressed = SAFETY_DIST;
                    btn_end_pressed = 0;

                    currentPosition = 0;
                    if (direction == BACKWARD)
                    {
                        targetPosition = 0;
                    }
                }
                else if (io_num == GPIO_BTN_END)
                {
                    btn_end_pressed = SAFETY_DIST;
                    btn_start_pressed = 0;
                }
            }
        }
    }
}

static void gpio_initialize()
{
    gpio_config_t io_conf;
    //bit mask of the pins that you want to set
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //disable pull-down mode
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    //disable pull-up mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    //bit mask of the pins
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    //interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //disable pull-down mode
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    //enable pull-up mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    //start gpio task
    xTaskCreate(gpio_task, "gpio_task", 2048, NULL, 10, NULL);

    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_BTN_START, gpio_isr_handler, (void *)GPIO_BTN_START);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_BTN_END, gpio_isr_handler, (void *)GPIO_BTN_END);
}

#endif /* GPIO_H */