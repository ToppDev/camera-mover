#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_pm.h"
#include "nvs_flash.h"

#include <string.h>
#include <sys/param.h>
#include "esp_system.h"
#include "tcpip_adapter.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "esp32/clk.h"

#define CONFIG_IPV4 1
#define PORT 65435U

#include "MoveHelper.h"
#include "wifi.h"
#include "TimerManager.h"

double feedrate = 550.0; ///< [mm / min] Feedrate

uint64_t targetPosition = 0;
int64_t currentPosition = 0;
DIRECTION direction = FORWARD;
bool automatic = true;
double automaticMoveDistanceMM = 100;
double automaticMoveIntervalSec = 30 * 60;

#include "gpio.h"

static const char *TAG = "CameraMover";

bool starts_with(const char *restrict string, const char *restrict prefix)
{
    while (*prefix)
    {
        if (*prefix++ != *string++)
            return 0;
    }

    return 1;
}

void setDirection(DIRECTION dir)
{
    direction = dir;
    gpio_set_level(GPIO_DIR, !direction);
    ESP_LOGI(TAG, "Setting Direction = %s", (direction == FORWARD ? "Forward" : "Backward"));
}

static void udp_server_task(void *pvParameters)
{
    char rx_buffer[128];
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    while (1)
    {

#ifdef CONFIG_IPV4
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
#else // IPV6
        struct sockaddr_in6 dest_addr;
        bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(PORT);
        addr_family = AF_INET6;
        ip_protocol = IPPROTO_IPV6;
        inet6_ntoa_r(dest_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
#endif

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0)
        {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        }
        ESP_LOGI(TAG, "Socket bound, port %d", PORT);

        while (1)
        {

            ESP_LOGI(TAG, "Waiting for data");
            struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            // Error occurred during receiving
            if (len < 0)
            {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            }
            // Data received
            else
            {
                // Get the sender's ip address as string
                if (source_addr.sin6_family == PF_INET)
                {
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                }
                else if (source_addr.sin6_family == PF_INET6)
                {
                    inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
                }

                rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string...
                ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
                ESP_LOGI(TAG, "%s", rx_buffer);

                if (!strcmp(rx_buffer, "?Mode"))
                {
                    sprintf(rx_buffer, "Current Mode = %s", automatic ? "Automatic" : "Manual");
                }
                else if (starts_with(rx_buffer, "Mode="))
                {
                    if (!strcmp(rx_buffer + 5, "Automatic"))
                    {
                        automatic = 1;
                        sprintf(rx_buffer, "Setting Mode to Automatic");
                    }
                    else if (!strcmp(rx_buffer + 5, "Manual"))
                    {
                        automatic = 0;
                        sprintf(rx_buffer, "Setting Mode to Manual");
                    }
                    else
                    {
                        sprintf(rx_buffer, "Could not recognize the mode");
                    }
                }
                else if (!strcmp(rx_buffer, "?AutomaticMoveDistance"))
                {
                    sprintf(rx_buffer, "Current Automatic Move Distance = %f mm", automaticMoveDistanceMM);
                }
                else if (starts_with(rx_buffer, "AutomaticMoveDistance="))
                {
                    automaticMoveDistanceMM = atof(rx_buffer + 22);
                    sprintf(rx_buffer, "Setting Automatic Move Distance to %f mm", automaticMoveDistanceMM);
                }
                else if (!strcmp(rx_buffer, "?AutomaticMoveInterval"))
                {
                    sprintf(rx_buffer, "Current Automatic Move Interval = %f s", automaticMoveIntervalSec);
                }
                else if (starts_with(rx_buffer, "AutomaticMoveInterval="))
                {
                    automaticMoveIntervalSec = atof(rx_buffer + 22);
                    sprintf(rx_buffer, "Setting Automatic Move Interval to %f s", automaticMoveIntervalSec);
                }
                else if (!strcmp(rx_buffer, "?Pos"))
                {
                    sprintf(rx_buffer, "Current Position = %f mm (%lld steps)", steps2mm(currentPosition), currentPosition);
                }
                else if (starts_with(rx_buffer, "Pos="))
                {
                    double targetPositionMM = atof(rx_buffer + 4);
                    if (targetPositionMM < 0)
                    {
                        sprintf(rx_buffer, "Negative Positions not allowed");
                    }
                    else
                    {
                        uint64_t newTargetPosition = mm2steps(targetPositionMM);
                        // Set direction
                        setDirection(newTargetPosition > currentPosition);

                        if (direction == FORWARD && gpio_get_level(GPIO_BTN_END))
                        {
                            sprintf(rx_buffer, "Can't move forward, because end button is pressed");
                        }
                        else if (direction == BACKWARD && gpio_get_level(GPIO_BTN_START))
                        {
                            sprintf(rx_buffer, "Can't move backward, because start button is pressed");
                        }
                        else
                        {
                            sprintf(rx_buffer, "Target Position  = %f mm (%lld steps)", targetPositionMM, newTargetPosition);
                            targetPosition = newTargetPosition;

                            if (targetPosition != currentPosition)
                            {
                                timer_start(TIMER_GROUP_0, TIMER_0);
                            }
                        }
                    }
                }
                else if (starts_with(rx_buffer, "Resume") || starts_with(rx_buffer, "Start"))
                {
                    timer_start(TIMER_GROUP_0, TIMER_0);
                }
                else if (starts_with(rx_buffer, "Pause") || starts_with(rx_buffer, "Stop"))
                {
                    timer_pause(TIMER_GROUP_0, TIMER_0);
                }
                else if (starts_with(rx_buffer, "Home"))
                {
                    if (!btn_start_pressed && !gpio_get_level(GPIO_BTN_START))
                    {
                        setDirection(BACKWARD);

                        targetPosition = 0;
                        currentPosition = mm2steps(700);

                        sprintf(rx_buffer, "Going Home");

                        timer_start(TIMER_GROUP_0, TIMER_0);
                    }
                    else
                    {
                        sprintf(rx_buffer, "Already Home");
                    }
                }
                else if (!strcmp(rx_buffer, "?Home"))
                {
                    sprintf(rx_buffer, "%s: %d", gpio_get_level(GPIO_BTN_START) ? "Is Home" : "Not Home", btn_start_pressed);
                }
                else if (!strcmp(rx_buffer, "?End"))
                {
                    sprintf(rx_buffer, "%s: %d", gpio_get_level(GPIO_BTN_END) ? "Is End" : "Not End", btn_end_pressed);
                }
                else if (!strcmp(rx_buffer, "?Feedrate"))
                {
                    sprintf(rx_buffer, "Current Feedrate = %f mm/min (delay = %f s)", feedrate, feedrate2delay(feedrate));
                }
                else if (starts_with(rx_buffer, "Feedrate="))
                {
                    feedrate = atof(rx_buffer + 9);
                    tg0_timer_set_interval(feedrate2delay(feedrate));
                    sprintf(rx_buffer, "New Feedrate = %f mm/min (delay = %f s)", feedrate, feedrate2delay(feedrate));
                }
                else
                {
                    sprintf(rx_buffer, "Unrecognized Command");
                }

                int err = sendto(sock, rx_buffer, strlen(rx_buffer), 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                if (err < 0)
                {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    break;
                }
            }
        }

        if (sock != -1)
        {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

void IRAM_ATTR timer_group0_isr(void *param)
{
    /* Clear the interrupt bit */
    TIMERG0.int_clr_timers.t0 = 1;
    /* After the alarm has been triggered
      we need enable it again, so it is triggered the next time */
    TIMERG0.hw_timer[0].config.alarm_en = TIMER_ALARM_EN;

    if (btn_start_pressed && direction == BACKWARD)
    {
        currentPosition = 0;
        targetPosition = 0;
        return;
    }
    if (btn_end_pressed && direction == FORWARD)
    {
        return;
    }

    // Toggle Step Pin
    gpio_set_level(GPIO_STEP, !*(int *)param);
    *(int *)param = !*(int *)param;

    if (*(int *)param)
    {
        if (direction == FORWARD)
        {
            if (btn_start_pressed)
            {
                btn_start_pressed--;
            }
            currentPosition++;
        }
        else
        {
            if (btn_end_pressed)
            {
                btn_end_pressed--;
            }
            currentPosition--;
        }
    }

    if (targetPosition == currentPosition)
    {
        timer_pause(TIMER_GROUP_0, TIMER_0);
    }
}

void app_main()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi
    wifi_power_save();

    // Create UDP Server Task
    xTaskCreate(udp_server_task, "udp_server", 4096, NULL, 5, NULL);

    // Initialize GPIOs
    gpio_initialize();
    // Initialize the move timer
    tg0_timer_init(feedrate2delay(feedrate));

    // Go Home
    if (!btn_start_pressed && !gpio_get_level(GPIO_BTN_START))
    {
        setDirection(BACKWARD);

        targetPosition = 0;
        currentPosition = mm2steps(700);
    }

    while (1)
    {
        while (automatic)
        {
            if (direction == FORWARD)
            {
                if (!btn_end_pressed && !gpio_get_level(GPIO_BTN_END))
                {
                    targetPosition = currentPosition + mm2steps(automaticMoveDistanceMM);
                    timer_start(TIMER_GROUP_0, TIMER_0);
                    vTaskDelay(automaticMoveIntervalSec * 1000 / portTICK_PERIOD_MS);
                    break;
                }
            }
            else if (direction == BACKWARD)
            {
                if (!btn_start_pressed && !gpio_get_level(GPIO_BTN_START))
                {
                    if (currentPosition - mm2steps(automaticMoveDistanceMM) < 0)
                    {
                        targetPosition = 0;
                    }
                    else
                    {
                        targetPosition = currentPosition - mm2steps(automaticMoveDistanceMM);
                    }
                    timer_start(TIMER_GROUP_0, TIMER_0);
                    vTaskDelay(automaticMoveIntervalSec * 1000 / portTICK_PERIOD_MS);
                    break;
                }
            }

            setDirection(!direction);
        }
    }
}