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
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "sdkconfig.h"
#include "esp_log.h"


/*
 This code demonstrates how to use the SPI master half duplex mode to read/write (8-bit mode).
*/
#ifdef CONFIG_IDF_TARGET_ESP32
    #define SPI_HOST_ID VSPI_HOST
    //#define DMA_CHAN 2        // If use DMA, set this
    #define PIN_NUM_MISO 13
    #define PIN_NUM_MOSI 12
    #define PIN_NUM_CLK  5
    #define PIN_NUM_OE 4

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
static spi_device_interface_config_t device_config = {
    .command_bits = 0,
    .address_bits = 8,      // Adresse sur 7 bits + 1 bit R/~W
    .dummy_bits = 0,
    .mode = 0,                  // Je sais pas ce que c'est, la doc dit rien
    .duty_cycle_pos = 128,
    .cs_ena_pretrans = 0,
    .cs_ena_posttrans = 0,
    .clock_speed_hz = SPI_MASTER_FREQ_26M,
    .input_delay_ns = 20,      // Selon datasheet page 39
    .spics_io_num = -1,          // On utilise des CE à la main (GPIO) => pas de CE hardware
    .flags = SPI_DEVICE_HALFDUPLEX,
    .queue_size = 150, // À redéfinir, mis au pifomètre (un angle)
    //.pre_cb,
    //.post_cb,
};
static spi_transaction_t conf_transaction_tx = {
    .flags = 0,
    .cmd = 0,
    .addr = 0,      // À modifier avant envoi
    .length = 8,
    .rxlength = 0,
    .user = NULL,
    .tx_buffer = NULL,  // À modifier avant envoi
    .rx_buffer = NULL,
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
static bool polling_started = false;

void spi_recv_table (unsigned char* table, unsigned int angle, unsigned int led, unsigned int rgb){
    img_apr_table = table;
    t_angle = angle;
    t_led = led;
    t_rgb = rgb;
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

    // --- Initialisation GPIO CE
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    for (unsigned char i = 0; i < sizeof(SPI_CE_GPIO)/sizeof(SPI_CE_GPIO[0]); i++){
        //bit mask of the pins that you want to set,e.g.GPIO18/19
        io_conf.pin_bit_mask = BIT(SPI_CE_GPIO[i]) ;
        //configure GPIO with the given settings
        gpio_config(&io_conf);
        // Met le GPIO à 0 => CE désactivé
        gpio_set_level(SPI_CE_GPIO[i], 1);
    }

    // Initialise OE à 0 (constament enable)
    io_conf.pin_bit_mask = BIT(PIN_NUM_OE) ;
    gpio_config(&io_conf);
    gpio_set_level(PIN_NUM_OE, 0);
}



void spi_send_data (unsigned char addr, unsigned char* data, int chipIndex, bool queue){
    /*
    Transmission au PCA9745B
    Entrée data par pointeur, transmission directe sans queue,
    met à jour le CE par GPIO : -1 si ne doit pas modifier les CE
    */
    // Met à jour les valeurs
    // On implémente à la fin de l'adresse le bit R/~W (à 0 car en écriture)
    conf_transaction_tx.addr = (addr << 1) & 0xFE;
    conf_transaction_tx.tx_buffer = data;
    // Met le CE à 0 si requis (CE activé)
    if (chipIndex != -1){
        gpio_set_level(SPI_CE_GPIO[chipIndex], 0);
    }
    // Réalise la transmission
    if (queue){
        spi_device_queue_trans(spi_device_handle, &conf_transaction_tx, portMAX_DELAY);
    } else {
        // On commence par vérifier que la précédente écriture aie fini (sinon erreur)
        // Attention à ne pas le faire sans avoir lancer une transmission avant
        // sinon erreur
        /*if (polling_started){
            spi_device_polling_end(spi_device_handle, portMAX_DELAY);
        } else {
            polling_started = true;
        }*/
        // En polling pour optimiser la vitesse d'execution, pas de timeout
        // Lance l'écriture, mais n'attent pas sa fin (on continue sans qu'elle soit finie)
        spi_device_polling_transmit(spi_device_handle, &conf_transaction_tx);
    } 
    // Met le CE à 1 si requis (CE désactivé)
    if (chipIndex != -1){
        gpio_set_level(SPI_CE_GPIO[chipIndex], 1);
    }
}

/*void spi_read_data (unsigned char addr, unsigned char* rx_buffer, int chipIndex){
    
    Lecture du PCA9745B
    Transmission directe sans queue,
    met à jour le CE par GPIO : -1 si ne doit pas modifier les CE
    
    // Met à jour les valeurs
    // On implémente à la fin de l'adresse le bit R/~W (à 1 car en lecture)
    conf_transaction_tx.addr = (addr << 1) | 0x01;
    conf_transaction_rx.rx_buffer = rx_buffer;
    // Met le CE à 0 si requis (CE activé)
    if (chipIndex != -1){
        gpio_set_level(SPI_CE_GPIO[chipIndex], 0);
    }
    // Réalise la transmission
    spi_device_transmit(spi_device_handle, &conf_transaction_rx);
    // Met le CE à 1 si requis (CE désactivé)
    if (chipIndex != -1){
        gpio_set_level(SPI_CE_GPIO[chipIndex], 1);
    }
}*/

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
    };
    //Initialize the SPI bus
    ret = spi_bus_initialize(SPI_HOST_ID, &buscfg, 0);
    ESP_ERROR_CHECK(ret);

#ifdef CONFIG_EXAMPLE_INTR_USED
    spi_config.intr_used = true;
    gpio_install_isr_service(0);
#endif
    ESP_LOGI(TAG_SPI, "SPI bus %d sucessfully initialised!", SPI_HOST_ID+1);
}



void spi_apr_draw_angle (unsigned int angle){
    // Affiche sur toute la bande un angle, de 0 à t_angle //
    // On commence par définir notre pointeur de référence (première valeur de l'angle)
    const unsigned char* table_angle_point = img_apr_table + (angle * (t_led+t_rgb));
    // On traversse le tableau sur l'angle qu'on a besoin de faire
    // On décompose la traverssée en composante (combine led et RGB) mais aussi device pour passer de l'un à l'autre
    // du coups on gère les CE à la main
    // En parralèle à device, a_device permet de poursuivre l'addition des valeurs dans la table
    unsigned int a_device = 0;
    for (unsigned char device = 0; device < SPI_CE_LEN; device += 1){
        a_device = device * t_led * t_rgb;
        //printf("MaxLast: %d %d %d\n", t_angle, t_led, t_rgb);
        for (unsigned int composante = 0; composante < ((t_led*t_rgb) / SPI_CE_LEN); composante++){
            //printf("Device: %d , Composante: %d , Value: %d \n", device, composante, *(table_angle_point+a_device+composante));
            spi_send_data(
                SPI_DEVICE_LED_ADDR+composante,
                (unsigned char*)(table_angle_point+a_device+composante),
                device,
                false);
        }
    }
}



void spi_apr_setup (void){
    /*
    Initialisationd des PCA9745B
    */
    // On commence par initialiser les GPIO qui font /CE (Chip Enable)
    spi_device_setup();

    // Acquire le spi pour réduire les pertes de temps entre les écritures
    spi_device_acquire_bus(spi_device_handle, portMAX_DELAY);

    // On définit le courant max dans nos LED
    // Iled = 20[mA] => 0x58 (réduit à 0x50)
    // Registre IrefAll => 0x41
    unsigned char* data = malloc(sizeof(unsigned char));
    *data = 0x04;       // pour ne pas se brûler la rétine, un 0x04 est bien
    for (unsigned char i = 0; i < sizeof(SPI_CE_GPIO)/sizeof(SPI_CE_GPIO[0]); i++){
        spi_send_data(0x41, data, i, false);
    }
    
}





/*void spi_apr_scan_chip (){
    
    Vérification que les chips sont là, sinon erreur
    
   // Initialisation 
   for (uint8 i = 0; i < sizeof(SPI_CE_GPIO); i++){
       spi_device_transmit(spi_device_handle_t handle, spi_transaction_t *trans_desc)
   }
}*/













#endif // _JUKEBOX_APR_SPI_