/**************************************************************************//**
 * @file     startup_ARMCM4.c
 * @brief    CMSIS Core Device Startup File for
 *           ARMCM4 Device
 * @version  V5.3.1
 * @date     09. July 2018
 ******************************************************************************/
/*
 * Copyright (c) 2009-2018 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Modified for TM4C123 by Raphael Kriegl
 */

/*
//-------- <<< Use Configuration Wizard in Context Menu >>> ------------------
*/

#include CMSIS_HEADER


/*----------------------------------------------------------------------------
  Linker generated Symbols
 *----------------------------------------------------------------------------*/
extern uint32_t __etext;
extern uint32_t __data_start__;
extern uint32_t __data_end__;
extern uint32_t __copy_table_start__;
extern uint32_t __copy_table_end__;
extern uint32_t __zero_table_start__;
extern uint32_t __zero_table_end__;
extern uint32_t __bss_start__;
extern uint32_t __bss_end__;
extern uint32_t __estack;


/*----------------------------------------------------------------------------
  Exception / Interrupt Handler Function Prototype
 *----------------------------------------------------------------------------*/
typedef void( *pFunc )( void );


/*----------------------------------------------------------------------------
  External References
 *----------------------------------------------------------------------------*/
extern void _start     (void) __attribute__((noreturn)); /* PreeMain (C library entry point) */


/*----------------------------------------------------------------------------
  Internal References
 *----------------------------------------------------------------------------*/
void Default_Handler(void) __attribute__ ((noreturn));
void Reset_Handler  (void) __attribute__ ((noreturn));


/*----------------------------------------------------------------------------
  User Initial Stack & Heap
 *----------------------------------------------------------------------------*/
//<h> Stack Configuration
//  <o> Stack Size (in Bytes) <0x0-0xFFFFFFFF:8>
//</h>
// #define  __STACK_SIZE  0x00000800
// static uint8_t stack[__STACK_SIZE] __attribute__ ((aligned(8), used, section(".stack")));

// //<h> Heap Configuration
// //  <o>  Heap Size (in Bytes) <0x0-0xFFFFFFFF:8>
// //</h>
// #define  __HEAP_SIZE   0x00002000
// #if __HEAP_SIZE > 0
// static uint8_t heap[__HEAP_SIZE]   __attribute__ ((aligned(8), used, section(".heap")));
// #endif


/*----------------------------------------------------------------------------
  Exception / Interrupt Handler
 *----------------------------------------------------------------------------*/
/* Exceptions */
void NMI_Handler            (void) __attribute__ ((weak, alias("Default_Handler")));
void HardFault_Handler      (void) __attribute__ ((weak, alias("Default_Handler")));
void MemManage_Handler      (void) __attribute__ ((weak, alias("Default_Handler")));
void BusFault_Handler       (void) __attribute__ ((weak, alias("Default_Handler")));
void UsageFault_Handler     (void) __attribute__ ((weak, alias("Default_Handler")));
void SVC_Handler            (void) __attribute__ ((weak, alias("Default_Handler")));
void DebugMon_Handler       (void) __attribute__ ((weak, alias("Default_Handler")));
void PendSV_Handler         (void) __attribute__ ((weak, alias("Default_Handler")));
void SysTick_Handler        (void) __attribute__ ((weak, alias("Default_Handler")));
extern void FLASH_IRQHandler       (void);

void Interrupt0_Handler     (void) __attribute__ ((weak, alias("Default_Handler")));
void Interrupt1_Handler     (void) __attribute__ ((weak, alias("Default_Handler")));
void Interrupt2_Handler     (void) __attribute__ ((weak, alias("Default_Handler")));
void Interrupt3_Handler     (void) __attribute__ ((weak, alias("Default_Handler")));
void Interrupt4_Handler     (void) __attribute__ ((weak, alias("Default_Handler")));
void Interrupt5_Handler     (void) __attribute__ ((weak, alias("Default_Handler")));
void Interrupt6_Handler     (void) __attribute__ ((weak, alias("Default_Handler")));
void Interrupt7_Handler     (void) __attribute__ ((weak, alias("Default_Handler")));
void Interrupt8_Handler     (void) __attribute__ ((weak, alias("Default_Handler")));
void Interrupt9_Handler     (void) __attribute__ ((weak, alias("Default_Handler")));


/*----------------------------------------------------------------------------
  Exception / Interrupt Vector table
 *----------------------------------------------------------------------------*/
extern const pFunc g_pfnVectors[240];
       const pFunc g_pfnVectors[240] __attribute__ ((section(".vectors"))) = {
  (pFunc)(&__estack),                     /*     Initial Stack Pointer */
  Reset_Handler,                            /*     Reset Handler */
  NMI_Handler,                              /* -14 NMI Handler */
  HardFault_Handler,                        /* -13 Hard Fault Handler */
  MemManage_Handler,                        /* -12 MPU Fault Handler */
  BusFault_Handler,                         /* -11 Bus Fault Handler */
  UsageFault_Handler,                       /* -10 Usage Fault Handler */
  0,                                        /*     Reserved */
  0,                                        /*     Reserved */
  0,                                        /*     Reserved */
  0,                                        /*     Reserved */
  SVC_Handler,                              /*  -5 SVCall Handler */
  DebugMon_Handler,                         /*  -4 Debug Monitor Handler */
  0,                                        /*     Reserved */
  PendSV_Handler,                           /*  -2 PendSV Handler */
  SysTick_Handler,                          /*  -1 SysTick Handler */

  /* Interrupts */                      // * only on TM4C1294!!
  Default_Handler,                      // GPIO Port A
  Default_Handler,                      // GPIO Port B
  Default_Handler,                      // GPIO Port C
  Default_Handler,                      // GPIO Port D
  Default_Handler,                      // GPIO Port E
  Default_Handler,                      // UART0 Rx and Tx
  Default_Handler,                      // UART1 Rx and Tx
  Default_Handler,                      // SSI0 Rx and Tx
  Default_Handler,                      // I2C0 Master and Slave
  Default_Handler,                      // PWM Fault
  Default_Handler,                      // PWM Generator 0
  Default_Handler,                      // PWM Generator 1
  Default_Handler,                      // PWM Generator 2
  Default_Handler,                      // Quadrature Encoder 0
  Default_Handler,                      // ADC Sequence 0
  Default_Handler,                      // ADC Sequence 1
  Default_Handler,                      // ADC Sequence 2
  Default_Handler,                      // ADC Sequence 3
  Default_Handler,                      // Watchdog timer
  Default_Handler,                      // Timer 0 subtimer A
  Default_Handler,                      // Timer 0 subtimer B
  Default_Handler,                      // Timer 1 subtimer A
  Default_Handler,                      // Timer 1 subtimer B
  Default_Handler,                      // Timer 2 subtimer A
  Default_Handler,                      // Timer 2 subtimer B
  Default_Handler,                      // Analog Comparator 0
  Default_Handler,                      // Analog Comparator 1
  0,                      // Analog Comparator 2 *
  Default_Handler,                      // System Control (PLL, OSC, BO)
  FLASH_IRQHandler,                      // FLASH Control
  0,                      // GPIO Port F *
  0,                      // GPIO Port G *
  0,                      // GPIO Port H *
  Default_Handler,                      // UART2 Rx and Tx
  Default_Handler,                      // SSI1 Rx and Tx
  Default_Handler,                      // Timer 3 subtimer A
  Default_Handler,                      // Timer 3 subtimer B
  Default_Handler,                      // I2C1 Master and Slave
  Default_Handler,                      // Quadrature Encoder 1
  Default_Handler,                      // CAN0
  Default_Handler,                      // CAN1
  0,                                      // Reserved
  0,                                      // Reserved
  Default_Handler,                      // Hibernate
  Default_Handler,                      // USB0
  Default_Handler,                      // PWM Generator 3
  Default_Handler,                      // uDMA Software Transfer
  Default_Handler,                      // uDMA Error
  Default_Handler,                      // ADC1 Sequence 0
  Default_Handler,                      // ADC1 Sequence 1
  Default_Handler,                      // ADC1 Sequence 2
  Default_Handler,                      // ADC1 Sequence 3
  0,                                      // Reserved
  0,                                      // Reserved
  0,                      // GPIO Port J *
  0,                      // GPIO Port K *
  0,                      // GPIO Port L *
  Default_Handler,                      // SSI2 Rx and Tx
  Default_Handler,                      // SSI3 Rx and Tx
  Default_Handler,                      // UART3 Rx and Tx
  Default_Handler,                      // UART4 Rx and Tx
  Default_Handler,                      // UART5 Rx and Tx
  Default_Handler,                      // UART6 Rx and Tx
  Default_Handler,                      // UART7 Rx and Tx
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  Default_Handler,                      // I2C2 Master and Slave
  Default_Handler,                      // I2C3 Master and Slave
  Default_Handler,                      // Timer 4 subtimer A
  Default_Handler,                      // Timer 4 subtimer B
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  0,                                      // Reserved
  Default_Handler,                      // Timer 5 subtimer A
  Default_Handler,                      // Timer 5 subtimer B
  Default_Handler,                      // Wide Timer 0 subtimer A
  Default_Handler,                      // Wide Timer 0 subtimer B
  Default_Handler,                      // Wide Timer 1 subtimer A
  Default_Handler,                      // Wide Timer 1 subtimer B
  Default_Handler,                      // Wide Timer 2 subtimer A
  Default_Handler,                      // Wide Timer 2 subtimer B
  Default_Handler,                      // Wide Timer 3 subtimer A
  Default_Handler,                      // Wide Timer 3 subtimer B
  Default_Handler,                      // Wide Timer 4 subtimer A
  Default_Handler,                      // Wide Timer 4 subtimer B
  Default_Handler,                      // Wide Timer 5 subtimer A
  Default_Handler,                      // Wide Timer 5 subtimer B
  Default_Handler,                      // FPU
  0,                                      // Reserved
  0,                                      // Reserved
  0,                      // I2C4 Master and Slave
  0,                      // I2C5 Master and Slave
  0,                      // GPIO Port M
  0,                      // GPIO Port N
  0,                      // Quadrature Encoder 2
  0,                                      // Reserved
  0,                                      // Reserved
  0,                      // GPIO Port P (Summary or P0)
  0,                      // GPIO Port P1
  0,                      // GPIO Port P2
  0,                      // GPIO Port P3
  0,                      // GPIO Port P4
  0,                      // GPIO Port P5
  0,                      // GPIO Port P6
  0,                      // GPIO Port P7
  0,                      // GPIO Port Q (Summary or Q0)
  0,                      // GPIO Port Q1
  0,                      // GPIO Port Q2
  0,                      // GPIO Port Q3
  0,                      // GPIO Port Q4
  0,                      // GPIO Port Q5
  0,                      // GPIO Port Q6
  0,                      // GPIO Port Q7
  0,                      // GPIO Port R
  0,                      // GPIO Port S
  Default_Handler,                      // PWM 1 Generator 0
  Default_Handler,                      // PWM 1 Generator 1
  Default_Handler,                      // PWM 1 Generator 2
  Default_Handler,                      // PWM 1 Generator 3
  Default_Handler                       // PWM 1 Fault
};


/*----------------------------------------------------------------------------
  Reset Handler called on controller reset
 *----------------------------------------------------------------------------*/
void Reset_Handler(void) {
  uint32_t *pSrc, *pDest;
  uint32_t *pTable __attribute__((unused));

/* Firstly it copies data from read only memory to RAM.
 * There are two schemes to copy. One can copy more than one sections.
 * Another can copy only one section. The former scheme needs more
 * instructions and read-only data to implement than the latter.
 * Macro __STARTUP_COPY_MULTIPLE is used to choose between two schemes.
 */

#ifdef __STARTUP_COPY_MULTIPLE
/* Multiple sections scheme.
 *
 * Between symbol address __copy_table_start__ and __copy_table_end__,
 * there are array of triplets, each of which specify:
 *   offset 0: LMA of start of a section to copy from
 *   offset 4: VMA of start of a section to copy to
 *   offset 8: size of the section to copy. Must be multiply of 4
 *
 * All addresses must be aligned to 4 bytes boundary.
 */
  pTable = &__copy_table_start__;

  for (; pTable < &__copy_table_end__; pTable = pTable + 3) {
    pSrc  = (uint32_t*)*(pTable + 0);
    pDest = (uint32_t*)*(pTable + 1);
    for (; pDest < (uint32_t*)(*(pTable + 1) + *(pTable + 2)) ; ) {
      *pDest++ = *pSrc++;
    }
  }
#else
/* Single section scheme.
 *
 * The ranges of copy from/to are specified by following symbols
 *   __etext: LMA of start of the section to copy from. Usually end of text
 *   __data_start__: VMA of start of the section to copy to
 *   __data_end__: VMA of end of the section to copy to
 *
 * All addresses must be aligned to 4 bytes boundary.
 */
  pSrc  = &__etext;
  pDest = &__data_start__;

  for ( ; pDest < &__data_end__ ; ) {
    *pDest++ = *pSrc++;
  }
#endif /*__STARTUP_COPY_MULTIPLE */




/* Single BSS section scheme.
 *
 * The BSS section is specified by following symbols
 *   __bss_start__: start of the BSS section.
 *   __bss_end__: end of the BSS section.
 *
 * Both addresses must be aligned to 4 bytes boundary.
 */
  pDest = &__bss_start__;

  for ( ; pDest < &__bss_end__ ; ) {
    *pDest++ = 0UL;
  }

  // Enable the floating-point unit.  This must be done here to handle the
  // case where main() uses floating-point and the function prologue saves
  // floating-point registers (which will fault if floating-point is not
  // enabled).  Any configuration of the floating-point unit using DriverLib
  // APIs must be done here prior to the floating-point unit being enabled.
  //
  // Note that this does not use DriverLib since it might not be included in
  // this project.
  //
#ifndef HWREG
#define HWREG(x) (*((volatile uint32_t *)(x)))
#endif
  HWREG(0xE000ED88) = ((HWREG(0xE000ED88) & ~0x00F00000) | 0x00F00000);

  //SystemInit();                             /* CMSIS System Initialization */
  _start();                                 /* Enter PreeMain (C library entry point) */
}


/*----------------------------------------------------------------------------
  Default Handler for Exceptions / Interrupts
 *----------------------------------------------------------------------------*/
void Default_Handler(void) {

  while(1);
}
