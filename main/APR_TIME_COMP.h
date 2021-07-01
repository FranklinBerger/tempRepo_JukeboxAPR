/*******************************
 * *** Affichage à Persistance Rétinienne *** *
 * ETML Préapp - 2020 2021
 * Auteur.e: Sleny Martinez
 * Détails:
 * Gestion du capteur à effet Hall, de la
 * task d'affichage des angles et du timer
 * pris comme base de temps pour la réalisation
 * de cela.
*******************************/

//*************************** Inclusions bibliothèques
// *** Interne
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <esp32/rom/ets_sys.h>
// *** Framework
#include "esp_types.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_task_wdt.h"
#include "driver/timer.h"
#include "driver/gpio.h"
//#include "gpio.h"
#include "esp32/rom/gpio.h"
// *** Fichiers Maison
#include "APR_SPI.h"


//*************************** Définitions constantes & Variables statiques
#define APR_TIME_COMP_TEST_MODE false
#define APR_TIMER_GROUP TIMER_GROUP_0
#define APR_TIMER_DIVIDER 2

#define APR_DRAWER_TIMER_INDEX TIMER_0
#define APR_DRAWER_INTR_MASK TIMER_INTR_T1
#define APR_DRAWER_INTR_FLAG NULL

#define APR_HALL_GPIO GPIO_NUM_35
#define APR_HALL_INTR_NUM 26
#define APR_HALL_INTR_FLAG ESP_INTR_FLAG_LEVEL3 | ESP_INTR_FLAG_IRAM

#define APR_QUEUE_HALL_ISR_VALUE NULL

#define TAG_TIME_APR "APR TIME"

static DRAM_ATTR uint64_t* timer_value = NULL;
// Pour ne pas avoir une surcharge d'interruption avant
// la première du captur hall, on initialise l'interruption
// à 1<<63 => 1.2 millénaire (pas d'interruption avant une
// première détection du capteur Hall)
static uint64_t step_delay = BIT64(63);
static uint64_t current_angle = 0;
static uint64_t timer_value_to_wait = 0;
//static TaskHandle_t apr_drawing_task_handle;
//static gpio_isr_handle_t apr_hall_isr_handle;
static portMUX_TYPE apr_time_comp_mutex;
//static xQueueHandle apr_time_comp_queue = NULL;
static void* random_buffer = NULL;
static DRAM_ATTR volatile bool apr_hall_isr_flag = false;
static DRAM_ATTR unsigned char* img_apr_table;
static DRAM_ATTR unsigned int t_angle;
static DRAM_ATTR unsigned int t_led;
static DRAM_ATTR unsigned int t_rgb;

static const timer_config_t apr_timer_config = {
    .alarm_en = TIMER_ALARM_DIS,        // Activé à la main si nécessaire
    .counter_en = TIMER_PAUSE,          // Activé en temps voulu
    .intr_type = TIMER_INTR_NONE,       // Activé à la main si nécessaire
    .counter_dir = TIMER_COUNT_UP,
    .auto_reload = TIMER_AUTORELOAD_EN,
    .divider = APR_TIMER_DIVIDER,
};
static const gpio_config_t apr_gpio_hall_config = {
    .pin_bit_mask = BIT64(APR_HALL_GPIO),
    .mode = GPIO_MODE_DEF_INPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_PIN_INTR_NEGEDGE,
};


//*************************** Code réel
// Interfaces
void timer_comp_recv_table (unsigned char* table, unsigned int angle, unsigned int led, unsigned int rgb){
    img_apr_table = table;
    t_angle = angle;
    t_led = led;
    t_rgb = rgb;
}


//Gestion des interruptions
// Interruption bas niveau de test
void IRAM_ATTR apr_hall_isr (){
    // Envoie un flag à la task principale
    // Actions nécessaires réalisées en son sein
    gpio_intr_disable(APR_HALL_GPIO);
    apr_hall_isr_flag = true;
    //hw->status1_w1tc.intr_st = mask;
    
    gpio_intr_enable(APR_HALL_GPIO);
    //gpio_intr_ack_high(APR_HALL_GPIO - 32);
    //gpio_ll_clear_intr_status_high(hal->dev, BIT(gpio_num - 32));
    //gpio_hal_get_intr_status_high()
    /*__asm__(
        "l32r a14, .GPIO_STATUS_W1TC_REG;"
        "l32r a15, .GPIO__NUM_30;"
        "s32i a15, a14, 0;");*/
    //gpio_set_level(PIN_NUM_OE, 0);
    //ets_printf("coucou");
}




// Action après interruptions
void apr_hall_isr_action (){
    /************************
    À la détection d'un champs magnétique par le capteur à effet Hall
    - On lis la valeur du timer
    - On remet le timer à 0
    - On redéfinit l'angle actuel à 0°
    - On calcul la nouvelle valeur de step_delay
    ************************/
    // On lis la valeur du timer
    timer_get_counter_value(APR_TIMER_GROUP, APR_DRAWER_TIMER_INDEX, timer_value);
    // On remet le timer à 0
    timer_set_counter_value(APR_TIMER_GROUP, APR_DRAWER_TIMER_INDEX, 0);
    // On redéfinit l'angle actuel à 0°
    // timer_value_to_wait = 0 permet de faire sauter l'attente à la drawing task
    // a.k.a: l'angle 0 s'affiche dès la détection du tour par le capteur Hall
    current_angle = 0;
    timer_value_to_wait = 0;
    // On calcul la nouvelle valeur de step_delay et on l'applique
    step_delay = *timer_value / t_angle;
    // On remet le timer à 0
    //timer_set_counter_value(APR_TIMER_GROUP, APR_DRAWER_TIMER_INDEX, 0);
    // Log pour débug => problèmes si dans une interruption
    #if APR_TIME_COMP_TEST_MODE == true
    ESP_LOGI(TAG_TIME_APR, "timer_value: %llu  step_delay: %llu", *timer_value, step_delay);
    #endif
}


// Task de gestion du tout
void apr_time_comp_task (void* args){
    /*
    Task constante de gestion de l'execution de l'affichage
    de l'APR
    - Commence par initialiser l'interruption du capteur hall

    - Vérifie si une détection s'est faite au niveau du capteur à effet Hall
        - Met à jour les valeurs de temps et d'angle
    - Vérifie si il est temps de mettre à jour l'affichage
        - Affiche
        - Incrémente la valeur de l'angle actuelle
        - Redéfini la prochaine valeur du timer à attendre
    - Recommence
    */
   // Initialisation cpateur Hall
   // On le fait ici pour être dans le deuxième coeur 
    //vPortCPUInitializeMutex(&apr_hall_isr_flag);
    ESP_INTR_DISABLE(APR_HALL_INTR_NUM);
    //intr_matrix_set( xPortGetCoreID(), ETS_GPIO_INTR_SOURCE, APR_HALL_INTR_NUM);
    // Interruption de test en C
    esp_intr_alloc( ETS_GPIO_INTR_SOURCE, APR_HALL_INTR_FLAG, apr_hall_isr, NULL, NULL ) ;
    ESP_INTR_ENABLE(APR_HALL_INTR_NUM);
    gpio_intr_enable(APR_HALL_GPIO);
    ESP_LOGI(TAG_TIME_APR, "Hall effect sensor interrupt initialised.");

    while (true){
        // Gestion détections du capteur Hall
        if (apr_hall_isr_flag){
            #if APR_TIME_COMP_TEST_MODE == true
            ESP_LOGI(TAG_TIME_APR, "Hall effect sensor interrupt executed.");
            #endif
            apr_hall_isr_flag = false;
            apr_hall_isr_action();
            /*spi_apr_draw_color(100);
            ets_delay_us(100); 
            spi_apr_draw_color(0);*/
        }

        // Gestion affichage selon timer
        timer_get_counter_value(APR_TIMER_GROUP, APR_DRAWER_TIMER_INDEX, timer_value);
        if (*timer_value >= timer_value_to_wait){
            // On est au bon moment pour afficher le nouvel angle
            // Affiche le nouvel angle
            // On fait en sorte de ne pas être dérangé pendant ce temps
            // (sauf si interruption)
            // Fait modulo pour continuer l'affichage même sans que le
            // capteur hall détecte un tour
            // Dans ce cas, un glitch se fera au tour suivant
            portENTER_CRITICAL(&apr_time_comp_mutex);
            if (current_angle < t_angle){
                spi_apr_draw_angle(current_angle);
            } else {
                spi_apr_draw_color(0);
            }
            portEXIT_CRITICAL(&apr_time_comp_mutex);
            // Log pour débug
            #if APR_TIME_COMP_TEST_MODE == true
            ESP_LOGI(TAG_TIME_APR, "Drawing angle %llu", current_angle);
            #endif
            // On incrément l'angle à afficher pour la prochaine fois
            current_angle++;
            // On recalcule la valeur à attendre
            timer_value_to_wait = current_angle * step_delay;
        }

        // Reset du watchdog
        esp_task_wdt_reset();
        // Puis on recommence
    }
}



// Initialisation
void apr_time_comp_init (){
    /*
    Initialisation de la composante temporelle de l'APR
    - Initialisation divers de variables
    - Interruption du capteur à effet Hall
    - Interruption du drawer
    - Timers
    */
    // Annonce en log
    ESP_LOGI(TAG_TIME_APR, "Start to initialise APR_TIME_COMP. Timer base clock: %d[Hz].", TIMER_BASE_CLK);
    // Initialisation de la mémoire où sera stoqué les valeurs lues du timer
    while (timer_value == NULL){
        ESP_LOGI(TAG_TIME_APR, "Try to initialise timer_value allocation of %d bytes.", sizeof(uint64_t));
        timer_value = malloc(sizeof(uint64_t));
    }

    // Initialisation du random_buffer
    while (random_buffer == NULL){
        ESP_LOGI(TAG_TIME_APR, "Try to initialise THE RANDOM BUFFER of %d bytes.", sizeof(uint8_t));
        random_buffer = malloc(sizeof(uint8_t));
    }

    // Initialisation du mutex pour les quelques critical sections
    // apr_hall_isr_flag est un mutext utilisé comme flag
    vPortCPUInitializeMutex(&apr_time_comp_mutex);

    // Initialisation du timer drawer et des interruptions de drawing
    ESP_ERROR_CHECK( timer_init(APR_TIMER_GROUP, APR_DRAWER_TIMER_INDEX, &apr_timer_config) );  // Init
    timer_set_counter_value(APR_TIMER_GROUP, APR_DRAWER_TIMER_INDEX, 0);                        // Mise à 0
    timer_start(APR_TIMER_GROUP, APR_DRAWER_TIMER_INDEX);

    
    // Initialisation du capteur Hall et de son interruption
    /*while (apr_time_comp_queue == NULL){
        ESP_LOGI(TAG_TIME_APR, "Try to initialise apr_hall_isr_queue.");
        apr_time_comp_queue = xQueueCreate(3, sizeof(NULL));
    }*/
    ESP_ERROR_CHECK( gpio_config(&apr_gpio_hall_config) );
    //ESP_ERROR_CHECK( gpio_install_isr_service((int)APR_HALL_INTR_FLAG) );
    //ESP_ERROR_CHECK( gpio_isr_handler_add(APR_HALL_GPIO, apr_hall_isr, &apr_hall_isr_flag) )  ;

    //ESP_ERROR_CHECK( gpio_isr_register(apr_hall_isr, NULL, (int) APR_HALL_INTR_FLAG, NULL) );

    // Initialisation de la task principale
    // Libère l'idle du watchdog pour éviter les erreurs (tâche constante)
    // Mais ajoute la tâche crée en échange
    // Priorité 9 pour laisse rla priorité à l'execution des interruptions
    /*portENTER_CRITICAL(&apr_time_comp_mutex);
    xTaskCreatePinnedToCore(apr_time_comp_task, "APR_Draw_TASK", 8192, NULL, 9, &apr_drawing_task_handle, APP_CPU_NUM);
    esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(1));
    esp_task_wdt_add(apr_drawing_task_handle);
    portEXIT_CRITICAL(&apr_time_comp_mutex);*/

    // Log pour débug
    ESP_LOGI(TAG_TIME_APR, "APR timer component successfully initialised.");
}