// Copy-edited from https://github.com/darthcloud/esp32_highint5_gpio

#include <xtensa/coreasm.h>
#include <xtensa/corebits.h>
#include <xtensa/config/system.h>
#include "freertos/xtensa_context.h"
#include "esp_private/panic_reason.h"
#include "sdkconfig.h"
#include "soc/soc.h"
#include "soc/gpio_reg.h"
#include "soc/dport_reg.h"

#define L5_INTR_STACK_SIZE  12
    .data
_l5_intr_stack:
    .space      L5_INTR_STACK_SIZE

    .section .iram1,"ax"
    .global     xt_highint5
    .type       xt_highint5,@function
    .align      4
xt_highint5:
    /* clearing the interrupt status of GPIO_NUM_35 */
    l32r a14, .GPIO_STATUS_W1TC_REG
    l32r a15, .GPIO__NUM_35
    s32i a15, a14, 0

    /* setting GPIO__NUM_4 on high */
    l32r a14, .GPIO_OUT_W1TS_REG
    l32r a15, .GPIO__NUM_4
    s32i a15, a14, 0

    /* Increment apr_hall_isr_flag */
    movi    a14, apr_hall_isr_flag
    l32i    a15, a14, 0
    addi    a15, a15, 1
    s32i    a15, a14, 0
    memw

    l32r a14, .GPIO_STATUS_REG
    l32i a14, a14, 0
    memw

    /* restore a0 */
    rsr     a0, EXCSAVE_5
    rfi     5

    /* The linker has no reason to link in this file; all symbols it exports are already defined
    in the default int handler. Define a symbol here so we can use it to have the
    linker inspect this anyway. */

    .global ld_include_xt_highint5
ld_include_xt_highint5:
