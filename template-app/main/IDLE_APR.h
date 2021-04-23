#ifndef _JUKEBOX_APR_IDLE_
#define _JUKEBOX_APR_IDLE_
/*******************************
 * *** IDLE.h - Affichage à Persistance Rétinienne *** *
 * ETML Préapp - 2020 2021
 * Auteur.e: Sleny Martinez
 * Détails:
 * Contient la fonction IDLE pour
 * l'ESP-IDF FreeRTOS. Celle-ci est chargée
 * de l'entretiens régulier du système
 * (en gros elle fait rien si ce n'est
 * rafraichir le watchdog pour par qu'il
 * nous emerde)
*******************************/
//*************************** Inclusions bibliothèques
#include "esp_task_wdt.h"
#include "esp_freertos_hooks.h"


//*************************** Inclusions bibliothèques
bool jukeboxAPR_RTOS_IDLE (){
    // On feed le watchdog
    esp_task_wdt_reset();

    // Return true permet de dire au RTOS
    // que l'IDLE n'as pas besoin d'être
    // rappelé rapidement
    return true;
}


#endif // _JUKEBOX_APR_IDLE_