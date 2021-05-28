#ifndef _JUKEBOX_APR_SPI_
#define _JUKEBOX_APR_SPI_
/* SPI Master Half Duplex
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
   Modified by Sleny Martinez under the same license.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <sys/types.h>

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_attr.h"


/*
 This code demonstrates how to use the SPI master half duplex mode to read/write (8-bit mode).
*/
#ifdef CONFIG_IDF_TARGET_ESP32
    #define SPI_HOST_ID VSPI_HOST
    #define DMA_CHAN 1        // If use DMA, set this
    #define PIN_NUM_MISO 13
    #define PIN_NUM_MOSI 12
    #define PIN_NUM_CLK  5
    #define PIN_NUM_CE 14
    #define PIN_NUM_OE 4
    
    #define TX_CHIPS 10
    #define TX_REGISTER 15

    //#define PIN_NUM_CS   13
#endif
#ifdef CONFIG_IDF_TARGET_ESP32S2
    #define SPI_HOST_ID    SPI2_HOST
    #define DMA_CHAN    SPI_HOST

    #define PIN_NUM_MISO 37
    #define PIN_NUM_MOSI 35
    #define PIN_NUM_CLK  36
    #define PIN_NUM_CS   34
#endif

static const char *TAG_SPI = "SPI";
static const unsigned char SPI_CE_GPIO[] = {14,15,16,17,18,19,21,22,23,25};
static const unsigned int SPI_CE_LEN = 10;
static const unsigned char SPI_DEVICE_LED_ADDR = 0x08;
static spi_device_handle_t spi_device_handle;
// Réglages device pour le PCA9745B
void IRAM_ATTR spi_ce_start_trans(void* arg);
void IRAM_ATTR spi_ce_stop_trans(void* arg);
static spi_device_interface_config_t device_config = {
    .command_bits = 0,
    .address_bits = 0,      // Pas d'adresse, tout est data
    .dummy_bits = 0,
    .mode = 0,                  // Je sais pas ce que c'est, la doc dit rien
    .duty_cycle_pos = 128,
    .cs_ena_pretrans = 2,
    .cs_ena_posttrans = 2,
    .clock_speed_hz = SPI_MASTER_FREQ_16M,
    .input_delay_ns = 20,      // Selon datasheet page 39
    .spics_io_num = PIN_NUM_CE,          // CE commun à tous les chips
    .flags = SPI_DEVICE_HALFDUPLEX,
    .queue_size = 150, // À redéfinir, mis au pifomètre (un angle)
    //.pre_cb = spi_ce_start_trans,
    //.post_cb = spi_ce_stop_trans,
};
/*static spi_transaction_t conf_transaction_rx = {
    .flags = 0,
    .cmd = 0,
    .addr = 0,      // À modifier avant envoi
    .length = 8,
    .rxlength = 8,
    .user = NULL,
    .tx_buffer = NULL,
    .rx_buffer = NULL,  // À modifier avant envoi
};*/
static unsigned char* img_apr_table;
static unsigned int t_angle;
static unsigned int t_led;
static unsigned int t_rgb;
// On doit jouer entre deux DMA pour pouvoir en modifier une pendant que
// l'autre est modifiée (et inversément). data_t correspond aux données en cours d'envois
// alors que data_w correspond aux données en cours d'écriture
// L'échange des registrer en t ou w se fait par spi_send_data
static unsigned char* data = NULL;    // Pointeur vers le packet de données en envois
//static unsigned char* data_w = NULL;    // Pointeur vers le packet de données en écriture
//static unsigned char* data_1 = NULL;    // Pointeur vers la première DMA
//static unsigned char* data_2 = NULL;    // Pointeur vers la deuxième DMA
//static bool data_flag = false;          // false = data_1 comme data_t, true = data_2 comme data_t
static bool polling_started = false;

void spi_recv_table (unsigned char* table, unsigned int angle, unsigned int led, unsigned int rgb){
    img_apr_table = table;
    t_angle = angle;
    t_led = led;
    t_rgb = rgb;
}

static spi_transaction_t conf_transaction_tx = {
    .flags = 0,
    .cmd = 0,
    .addr = 0,      
    .length = TX_CHIPS * 16,    // 16 x nbrChip
    .rxlength = 0,
    .user = NULL,
    .tx_buffer = NULL,
    .rx_buffer = NULL,
};



void spi_send_data (bool whaitEnd){
    /*
    Transmission au PCA9745B
    Entrée data par pointeur, transmission directe sans queue,
    met à jour le CE par GPIO : -1 si ne doit pas modifier les CE
    */
    // Réalise la transmission
    // On commence par vérifier que la précédente écriture aie fini (sinon erreur)
    // Attention à ne pas le faire sans avoir lancer une transmission avant
    // sinon erreur
    if (polling_started){
        spi_device_polling_end(spi_device_handle, portMAX_DELAY);
        // Délsactivation de toutes les pin CE pour terminer la transmission
    } else {
        polling_started = true;
    }

    // Rafraichis le pointeur vers la DMA
    /*if (data_flag){
        data_t = data_1;
        data_w = data_2;
    } else {
        data_t = data_2;
        data_w = data_1;
    }
    data_flag = !data_flag;*/
    

    // Transmission
    if (whaitEnd){
        // Transmission en attendant la fin dirrectement
        spi_device_polling_transmit(spi_device_handle, &conf_transaction_tx);
    } else {
        // En polling pour optimiser la vitesse d'execution, pas de timeout
        // Lance l'écriture, mais n'attent pas sa fin (on continue sans qu'elle soit finie)
        // On attend sa fin à la prochaine écriture
        spi_device_polling_start(spi_device_handle, &conf_transaction_tx, portMAX_DELAY);
    } 
}


void spi_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG_SPI, "Initializing bus SPI%d...", SPI_HOST_ID+1);
    spi_bus_config_t buscfg={
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
        .flags = 0,
    };
    //Initialize the SPI bus
    ret = spi_bus_initialize(SPI_HOST_ID, &buscfg, DMA_CHAN);
    ESP_ERROR_CHECK(ret);

#ifdef CONFIG_EXAMPLE_INTR_USED
    spi_config.intr_used = true;
    gpio_install_isr_service(0);
#endif
    ESP_LOGI(TAG_SPI, "SPI bus %d sucessfully initialised!", SPI_HOST_ID+1);
}


void spi_apr_draw_angle (unsigned int angle){
    // Affiche sur toute la bande un angle, de 0 à t_angle //
    if (angle > t_angle){
        ESP_LOGE(TAG_SPI, "angle N°%d out of range.", angle);
    } else {
        // On envoi la tramme par registre, on commence par traversser nos différents registres
        ESP_LOGV(TAG_SPI, "Show angle N°%d : %.*s", angle, (t_led * t_rgb), img_apr_table);
        unsigned char* dataAddr = NULL;
        for (unsigned char r = 0; r < TX_REGISTER; r++){
            // On commence par définir notre pointeur de référence (première valeur de l'angle au
            // registre sélectionné)
            unsigned char* table_angle_point = img_apr_table + (angle * t_led * t_rgb) + r;
            // On traversse le tableau sur l'angle qu'on a besoin de faire
            // On ne fait que faire des (10) sauts de 15, on prend la valeur, et on intercalle
            // l'adresse du registre à chaque fois
            // Dans la DDRAM, on écrit à l'envert (en premier les pixels extérieur)
            for (unsigned char c = 0; c < TX_CHIPS; c++){
                dataAddr = (data+(2*TX_CHIPS)) - (2*c);
                // Adresse + R/~W
                *(dataAddr) = ((SPI_DEVICE_LED_ADDR+r)<<1) & ~0x01;
                // Data
                *(dataAddr-1) = *(table_angle_point + (c*TX_REGISTER));
            }
            /*printf("Tramme N°%d: ", r);
            for (unsigned int i = 0; i < TX_CHIPS*2*sizeof(unsigned char); i++){
                printf("%d ",*(data + i));
            }
            printf("\n\n");*/
            // Puis on envoie la tramme (un registre)
            spi_send_data(false);
        }
    }
}

void spi_apr_draw_color (uint8_t color){
    // Affiche sur toute la bande un angle, de 0 à t_angle //
    // On envoi la tramme par registre, on commence par traversser nos différents registres
    for (unsigned char r = 0; r < TX_REGISTER; r++){
        // On commence par définir notre pointeur de référence (première valeur de l'angle au
        // registre sélectionné)
        // On traversse le tableau sur l'angle qu'on a besoin de faire
        // On ne fait que faire des (10) sauts de 15, on prend la valeur, et on intercalle
        // l'adresse du registre à chaque fois        
        for (unsigned char c = 0; c < TX_CHIPS; c++){
            // Adresse + R/~W
            *(data + (c*2)) = ((SPI_DEVICE_LED_ADDR+r)<<1) & ~0x01;
            // Data
            *(data + (c*2) + 1) = color;
        }
        /*printf("Tramme N°%d: ", r);
        for (unsigned int i = 0; i < TX_CHIPS*2*sizeof(unsigned char); i++){
            printf("%d ",*(data + i));
        }
        printf("\n\n");*/
        // Puis on envoie la tramme (un registre)
        spi_send_data(false);
    }
}


void spi_device_setup (void){
    /*
    Initialisationd des devices
    On utilise qu'un seul sans CE car le port SPI ne supporte pas
    plus de 3 CE (alors qu'on en a besoin de 10)
    Du coups, on initialise un device, les 10 GPIOs séparément
    et on s'arrange après à la main
    */
    // --- Initialisation device dans le SPI
    // Initialisation 
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST_ID, &device_config, &spi_device_handle));

    // Initialisation OE comme GPIO
    // Initialise OE à 0 (constament enable)
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up modex
    io_conf.pull_up_en = 0;
    io_conf.pin_bit_mask = BIT(PIN_NUM_OE) ;
    gpio_config(&io_conf);
    gpio_set_level(PIN_NUM_OE, 0);
}


void spi_apr_setup (void){
    /*
    Initialisationd des PCA9745B
    */
    // On commence par initialiser les GPIO qui font /CE (Chip Enable)
    spi_device_setup();

    // Acquire le spi pour réduire les pertes de temps entre les écritures
    spi_device_acquire_bus(spi_device_handle, portMAX_DELAY);

    // Initialise les données du SPI dans la DMA
    while (data == NULL){
        ESP_LOGI(TAG_SPI, "Initializing DMA Buffer of %d bits", TX_CHIPS*16);
        data = heap_caps_malloc(TX_CHIPS*2*sizeof(unsigned char), MALLOC_CAP_DMA);  //= malloc(sizeof(unsigned int));
    }
    /*while (data_2 == NULL){
        ESP_LOGI(TAG_SPI, "Initializing DMA2 Buffer of %d bits", TX_CHIPS*16);
        data_2 = heap_caps_malloc(TX_CHIPS*2*sizeof(unsigned char), MALLOC_CAP_DMA);  //= malloc(sizeof(unsigned int));
    }*/
    conf_transaction_tx.tx_buffer = data;

    // Initialisation des chips
    // On définit le courant max dans nos LED
    // Iled = 20[mA] => 0x58 (réduit à 0x50)
    // Registre IrefAll => 0x41
    // On remplis à la main car le reste se fait au sein de write_angle
    // Transmet addr = 0x41 data = 0x04 avec le bit R/~W = 0
    for (unsigned char i = 0; i < TX_CHIPS*2; i += 2){
        *(data+i) = (0x41<<1) & ~0x01;      // Adresse + bit R/~W
        *(data+i+1) = 0x15;                 // Data, MAX 0x50, 0x25 bien, 0x04 pour tests
    }
    spi_send_data(false);
    // On applique une gradation exponentielle pour améliorer
    // la correspondance des couleurs et luminosité
    // On remplis à la main car le reste se fait au sein de write_angle
    // Transmet addr = 0x01 data = 0x05 (0x01 par défaut | 0x04 pour gradation exponentielle)
    // avec le bit R/~W = 0
    /*for (unsigned char i = 0; i < TX_CHIPS*2; i += 2){
        *(data+i) = (0x01<<1) & ~0x01;      // Adresse + bit R/~W
        *(data+i+1) = 0x05;                 // Data, 0x50 (0x04 pour tests)
    }
    spi_send_data(false);*/
    

    /* --- Allume la première LED (0xFF, composante rouge)
    // Idem qu'avant mais avec addr = 0x08, R/~W = 0 et data = 0xFF
    for (unsigned char i = 0; i < TX_CHIPS*2; i += 2){
        *(data+i) = 0x10;
        *(data+i+1) = 0xff;
    }
    data = 0xFF10FF10FF10FF10FF10;
    spi_send_data(false);*/
}













#endif // _JUKEBOX_APR_SPI_