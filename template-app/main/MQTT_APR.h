#ifndef _JUKEBOX_APR_MQTT_
#define _JUKEBOX_APR_MQTT_
/* MQTT (over TCP) Example
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
   Modified by Sleny Martinez, under the same license.
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG_MQTT = "MQTT";

// Topic MQTT où se trouve l'image de l'APR
#define MQTT_IMG_TOPIC "test"

// Adresse du broker MQTT
#define CONFIG_BROKER_URL "mqtt://192.168.5.1"

// Nombre de tentatives de reconnexions au MQTT avant de reboot
#define RECONNECTION_BEFOR_ABORD 100

// Pour pouvoir y accéder dans les différentes fonctions
// apr_img_table = Pointeur original vers l'image
// apr_img_cur = Curseur (de 0 à len-1) de où en est l'écriture
// (les 54[Ko] de l'image sont reçues par le MQTT en packet de 1024[o]
// qu'on doit enregistrer un par un)
// apr_img_length = Valeur maximale du curseur
static unsigned char* apr_img_table = NULL;
static int apr_img_cur = 0;
static int apr_img_length = 0;

static void mqtt_recv_table (unsigned char* table, int length){
    apr_img_table = table;
    apr_img_cur = 0;
    apr_img_length = length;
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    int reconnections = 0;
    // your_context_t *context = event->context;
    switch (event->event_id) {


        //---------------------------- Connected
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_CONNECTED");
            
            msg_id = esp_mqtt_client_subscribe(client, MQTT_IMG_TOPIC, 1);
            ESP_LOGI(TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);

            break;


        //---------------------------- Disconnected
        case MQTT_EVENT_DISCONNECTED:
            do{
                ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DISCONNECTED, Try to reconnect for %d times", reconnections);
                if (++reconnections > RECONNECTION_BEFOR_ABORD){
                    ESP_LOGE(TAG_MQTT, "MQTT_EVENT_DISCONNECTED for too many times, Abord!");
                    abort();
                }
            } while (esp_mqtt_client_reconnect(client) != ESP_OK);
            break;


        //---------------------------- Subscribe
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        

        //---------------------------- Unsubscribe
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        

        //---------------------------- Publish
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        

        //---------------------------- Recieve Data
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DATA");
            ESP_LOGI(TAG_MQTT,"TOPIC=%.*s\r\n", event->topic_len, event->topic);
            if (event->data_len < 50){
                ESP_LOGI(TAG_MQTT, "DATA=%.*s\r\n", event->data_len, event->data);
            } else {
                ESP_LOGI(TAG_MQTT, "DATA_LENGTH=%d\r\n", event->data_len);
            }
            // Sauvegarde du packet de données
            // Sauvegarder au maximum sans dépasser la table
            if ( (event->data_len)+apr_img_cur<=apr_img_length ){
                // On ne dépasse pas la table => simple transfert de données
                memcpy((apr_img_table+apr_img_cur), 
                (event->data),
                (event->data_len));                
            } else {
                // Trop de données pour la table, on coupe pour ne pas dépasser
                memcpy((apr_img_table+apr_img_cur), 
                (event->data),
                apr_img_length - apr_img_cur);
            }
            // Incrémenttation du curseur et remise à 0 si on est au bout
            apr_img_cur = apr_img_cur + (event->data_len);
            apr_img_cur = apr_img_cur<apr_img_length ? apr_img_cur : 0;


            ESP_LOGI(TAG_MQTT, "MQTT data successfully saved!\n");
            break;
        

        //---------------------------- Error
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_ERROR");
            break;
        

        //---------------------------- Default
        default:
            ESP_LOGI(TAG_MQTT, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG_MQTT, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

static void mqtt_app_start(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    #ifndef _ESP_EVENT_LOOP_CREATE_DEFAULT_
    #define _ESP_EVENT_LOOP_CREATE_DEFAULT_
        ESP_ERROR_CHECK(esp_event_loop_create_default());
    #endif 

    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
    };
    #if CONFIG_BROKER_URL_FROM_STDIN
        char line[128];

        if (strcmp(mqtt_cfg.uri, "FROM_STDIN") == 0) {
            int count = 0;
            printf("Please enter url of mqtt broker\n");
            while (count < 128) {
                int c = fgetc(stdin);
                if (c == '\n') {
                    line[count] = '\0';
                    break;
                } else if (c > 0 && c < 127) {
                    line[count] = c;
                    ++count;
                }
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
            mqtt_cfg.uri = line;
            printf("Broker url: %s\n", line);
        } else {
            ESP_LOGE(TAG_MQTT, "Configuration mismatch: wrong broker url");
            abort();
        }
    #endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}

#endif // _JUKEBOX_APR_MQTT_