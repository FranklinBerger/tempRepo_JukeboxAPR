/*******************************
 * *** Affichage à Persistance Rétinienne *** *
 * ETML Préapp - 2020 2021
 * Auteur.e: Sleny Martinez
 * Détails:
 * Code pour l'affichage à persistance rétinienne
 * du projet commun Jukebox du préapprentisage
 * 2020-2021. L'image à afficher provient d'un
 * broker MQTT, puis on l'affiche sur des RGB
 * contrôlées par PCA9745B. Se référer aux
 * schémas et à la documentation pour plus de
 * détails.
*******************************/



//*************************** Inclusions bibliothèques
// *** Interne
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// *** Framework
#include "esp_pm.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
// *** Maison
#include "Wifi_APR.h"
#include "MQTT_APR.h"
#include "IDLE_APR.h"
#include "APR_SPI.h"
#include "APR_TIME_COMP.h"
//#include "EXAMPLE_IMAGE.H"



//*************************** Définitions constantes
// Mode débug ou fonctionnement normal
#define DEBUG_MODE true

// Taille de l'affichge
// T_ANGLE = Nombre de pas / changement de couleurs dans un tour (modifiable si adaptation du format MQTT)
#define T_ANGLE 360
// T_LED = Nombre de LED
#define T_LED 50
// T_RGB = Nonbre de couleurs par LED
#define T_RGB 3

// Période du Watchdog (en secondes)
#define TWDT_TIMEOUT_S 3

// Parce qu'on est des flemard
typedef unsigned char uint8;

// Fonction qui vérifie qu'aucune erreur ne se soit produite,
// sinon redémarre le système (abord)
// Privilégier les gestions d'erreurs
#define NO_ERROR_OR_ABORD(returned, expected) ({                       \
            if(returned != expected){                                  \
                printf("A fatal error has been detected. Abord!\n");   \
                abort();                                               \
            }                                                          \
})


//*************************** Code Main
void app_main(void)
{
    //**************** Initialisation
    // Explicite le système
    printf("*** System Info ***\n");
    printf("CPU Clock: %d\n", ets_get_xtal_scale());
    printf("Free heap Avalable: %d\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    printf("app_main taks priority: %d\n", uxTaskPriorityGet(NULL));
    printf("*******************\n");

    //Délais pour laisser le système s'allumer, nottament les chips
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Gestion du Log Level
    // Si on travail en mode debug => Verbose (Log toutes les informations)
    // Si on travail en mode normal => Error (Log uniquement les erreurs)
    printf("Start Setup of Log Level\n");
    esp_log_level_set("*", DEBUG_MODE ? ESP_LOG_VERBOSE : ESP_LOG_ERROR);


    // Initialisation du Watchdog
    // Réglé à 3 seconde, déclenché si en mode développement
    // Cation => Reset l'ESP
    // Utilisation du watchdog task, pas du watchdog interrupt
    // (par soucis de simplicité)
    // Fonctionnement normal
    // On met le mode Panic à true pour que si le watchdog est
    // déclenché, on redémarre le système
    // Debug Mode:
    // On met le mode Panic à false pour que si le watchdog est
    // déclenché, on ne redémarre pas le système
    printf("Start Setup of Watchdog\n");
    #ifndef CONFIG_ESP_TASK_WDT
        NO_ERROR_OR_ABORD(esp_task_wdt_init(TWDT_TIMEOUT_S, !DEBUG_MODE), ESP_OK);
    #endif
    #ifndef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
        //NO_ERROR_OR_ABORD(esp_task_wdt_add(xTaskGetIdleTaskHandleForCPU(0)), ESP_OK);
    #endif
    #ifndef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
        //NO_ERROR_OR_ABORD(esp_task_wdt_add(xTaskGetIdleTaskHandleForCPU(1)), ESP_OK);
    #endif

    NO_ERROR_OR_ABORD(esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0)), ESP_OK);


    // Génération de la pseudo-tableau contenant l'image
    // Elle est structurée de la manière suivante (tout en 1 octet):
    // Angle0LED0R Angle0LED0G Angle0LED0B Angle1LED0R Angle1LED0G Angle1LED0B etc...
    // Pour 360 pas par tour (T_ANGLE), 50 LED (T_LED) et 3 couleurs (T_RGB), on obtient
    // 54'000 octets de mémoire
    // Pour traversser la liste, voici la manière la plus simple de procéder:
    // for (int i = 0 ; i < T_ANGLE * T_LED ; i += T_LED * T_RGB) {
    //    for (int j = 0 ; j < T_LED ; j += T_RGB) {
    //        for (int k = 0; k < T_RGB; k++){
    //            // Ici il s'agit d'une assignation, mais on peut faire ce qu'on veut
    //            *(table+i+j+k) = 0xFF;
    //        }
    //    }
    // }
    printf("Start Setup of Image Table\n");
    printf("Free RAM Avalable: %d\n", esp_get_free_heap_size());
    printf("Largest free bloc: %d\n", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    printf("Table space required: %d\n", T_ANGLE * T_LED * T_RGB);
    // Pour intialiser la table, on utilise les fonctions de FreeRTOS par
    // soucis de compatibilité
    // On vérifie l'allocation réussie de la table, sinon on recommence
    //int *table = (int*) malloc(T_ANGLE * T_LED * T_RGB * sizeof(uint8));
    uint8 *apr_img_table = NULL;
    while (apr_img_table == NULL) {
        //printf("Failed to setup table, retrying...\n");
        //table = pvPortMallocAligned (T_ANGLE * T_LED * T_RGB * sizeof(uint8));
        apr_img_table = heap_caps_aligned_calloc(
            65536,                          // Par bloc de minimum 65536 octets (=> tout d'un bloc)
            (T_ANGLE+1) * T_LED * T_RGB,        // Nombre de bloc (les 54'000)
            sizeof(uint8),                  // Taille d'un bloc (uint8 => 8 bits)
            MALLOC_CAP_DEFAULT);            // Pas de spécificités supplémentaires (Default)
    }
    //memcpy(apr_img_table, &EXAMPLE_IMAGE, ((T_ANGLE+1) * T_LED * T_RGB));
    // Outil de test: Remplissage de la table
    // L'exemple ci-dessous fait un quart Rouge, un quart Vert, un quart Bleu, un quart Blanc
    /*for (int i = 0 ; i < T_ANGLE * T_LED ; i += T_LED * T_RGB) {
        for (int j = 0 ; j < T_LED ; j += T_RGB) {
            for (int k = 0; k < T_RGB; k++){
                // Ici il s'agit d'une assignation, mais on peut faire ce qu'on veut
                if (k == (j%3)){
                    *(apr_img_table+i+j+k) = 0x80;
                } else {
                    *(apr_img_table+i+j+k) = 0x00;
                }
            }
        }
    }*/
    printf("Table initialisation Sucess, Free RAM Avalable: %d\n", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));



    // Initialisation de l'IDLE pour l'ESP-IDF FreeRTOS
    // tâche par défaut: Ne fait (pratiquement) rien
    printf("Start Setup of IDLE\n");
    NO_ERROR_OR_ABORD(esp_register_freertos_idle_hook(jukeboxAPR_RTOS_IDLE), ESP_OK);
    esp_register_freertos_idle_hook_for_cpu(jukeboxAPR_RTOS_IDLE, tskNO_AFFINITY);



    // Initialisation Wifi
    printf("Start Setup of Wifi\n");
    wifi_init_sta();



    // Initialisation de la connection avec le broker MQTT
    mqtt_recv_table(apr_img_table, T_ANGLE*T_LED*T_RGB);    
    mqtt_app_start();


    // Initialisation Bus SPI
    spi_recv_table (apr_img_table, T_ANGLE, T_LED, T_RGB);
    spi_init();
    spi_apr_setup();


    // Initialisation du capteur Hall, Timer et écriture périodique des LED
    timer_comp_recv_table(apr_img_table, T_ANGLE, T_LED, T_RGB);
    apr_time_comp_init();


    printf("End of Initialisation, APR started.\n");

    apr_time_comp_task(NULL);
    //unsigned char color = 0;
    /*while (true){
        printf("\nMain Here\n");

        printf("Table 10 first bytes: ");for (uint8 i=0; i<10; i++){printf("%d ", *(apr_img_table+i));}
        printf("\nTable 10 last bytes: ");for (uint8 i=0; i<10; i++){printf("%d ", *(apr_img_table+(T_ANGLE*T_LED*T_RGB)-(10-i)));}printf("\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_task_wdt_reset();



        //spi_apr_draw_angle(0);


        for (int i = 0 ; i < T_ANGLE*T_LED*T_RGB ; i += T_LED*T_RGB) {
            for (int j = 0 ; j < T_LED*T_RGB ; j += T_RGB) {
                for (int k = 0; k < T_RGB; k++){
                    // Ici il s'agit d'une assignation, mais on peut faire ce qu'on veut
                    if (k == (j%3)){
                        *(apr_img_table+i+j+k) = 0x80;
                    } else {
                        *(apr_img_table+i+j+k) = 0x00;
                    }
                }
            }
        }
        color = (color+1)%3;    


    }*/







    /*


    // Liste créée, voilà comment la traversser / remplir
    for (int i = 0 ; i < T_ANGLE * T_LED ; i += T_LED * T_RGB) {
        for (int j = 0 ; j < T_LED ; j += T_RGB) {
            for (int k = 0; k < T_RGB; k++){
                *(table+i+j+k) = 0xFF;
            }
        }
    }

    printf("Finished to filling it!\nStart to verify!\n");

    //table[145][43][1] = 25;

    printf("Sizeof table of %d by %d by %d: %d\n", T_ANGLE, T_LED, T_RGB, sizeof(*(table)));
    
    for (int i = 0 ; i < T_ANGLE * T_LED ; i += T_LED * T_RGB) {
        for (int j = 0 ; j < T_LED ; j += T_RGB) {
            for (int k = 0; k < T_RGB; k++){
                if (*(table+i+j+k) == 0x12){
                    printf("Hello at %d %d %d\n", i, j, k);
                }
            }
        }
    }

    printf("End of verification\n"); 

    //printf(table);

    Print chip information 
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    for (int i = 10; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart(); */
}
