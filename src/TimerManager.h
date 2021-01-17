#ifndef TIMER_MANAGER_H
#define TIMER_MANAGER_H

#include "esp_log.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "driver/gpio.h"

#define TIMER_DIVIDER 16                             //  Hardware timer clock divider
#define TIMER_SCALE (TIMER_BASE_CLK / TIMER_DIVIDER) // convert counter value to seconds

static const char *TIMER_TAG = "TimerManager";

uint8_t spkr_pin = 1;

void IRAM_ATTR timer_group0_isr(void *param);

/*
 * Initialize selected timer of the timer group 0
 *
 * timer_interval_sec - the interval of alarm to set
 */
static void tg0_timer_init(double timer_interval_sec)
{
    timer_idx_t timer_idx = TIMER_0;
    /* Select and initialize basic parameters of the timer */
    timer_config_t config;
    config.divider = TIMER_DIVIDER;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.intr_type = TIMER_INTR_LEVEL;
    config.auto_reload = TIMER_AUTORELOAD_EN;
    timer_init(TIMER_GROUP_0, timer_idx, &config);

    /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
    timer_set_counter_value(TIMER_GROUP_0, timer_idx, 0x00000000ULL);

    ESP_LOGI(TIMER_TAG, "Trigger Timer every: %lf s", timer_interval_sec);
    ESP_LOGI(TIMER_TAG, "Timer Alarm Cnt: %lld", (uint64_t)(timer_interval_sec * TIMER_SCALE));

    /* Configure the alarm value and the interrupt on alarm. */
    timer_set_alarm_value(TIMER_GROUP_0, timer_idx, timer_interval_sec * TIMER_SCALE);
    timer_enable_intr(TIMER_GROUP_0, timer_idx);
    timer_isr_register(TIMER_GROUP_0, timer_idx, &timer_group0_isr,
                       &spkr_pin, ESP_INTR_FLAG_IRAM, NULL);
}

static void tg0_timer_set_interval(double timer_interval_sec)
{
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, timer_interval_sec * TIMER_SCALE);
}

#endif /* TIMER_MANAGER_H */