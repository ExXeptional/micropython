/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>

#include "py/runtime.h"
#include "rtc.h"
#include "irq.h"
#include "time.h"
#include "inc/hw_ints.h"
#include "inc/hw_sysctl.h"
#include "inc/hw_hibernate.h"
#include "driverlib/rom.h"
#include "driverlib/sysctl.h"
#include "driverlib/hibernate.h"
#include "driverlib/interrupt.h"

/// \moduleref pyb
/// \class RTC - real time clock
///
/// The RTC is and independent clock that keeps track of the date
/// and time.
///
/// Example usage:
///
///     rtc = pyb.RTC()
///     rtc.datetime((2014, 5, 1, 4, 13, 0, 0, 0))
///     print(rtc.datetime())



typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
}rtc_time_t;

rtc_time_t rtc_time;

// rtc_info indicates various things about RTC startup
// it's a bit of a hack at the moment
static mp_uint_t rtc_info;

// Note: LSI is around (32KHz), these dividers should work either way
// ck_spre(1Hz) = RTCCLK(LSE) /(uwAsynchPrediv + 1)*(uwSynchPrediv + 1)
// modify RTC_ASYNCH_PREDIV & RTC_SYNCH_PREDIV in board/<BN>/mpconfigport.h to change sub-second ticks
// default is 3906.25 µs, min is ~30.52 µs (will increas Ivbat by ~500nA)
#ifndef RTC_ASYNCH_PREDIV
#define RTC_ASYNCH_PREDIV (0x7f)
#endif
#ifndef RTC_SYNCH_PREDIV
#define RTC_SYNCH_PREDIV  (0x00ff)
#endif


// For converting rtc time to hours and minutes
#define SEC_HOUR 3600
#define SEC_MINUTE 60

//STATIC HAL_StatusTypeDef PYB_RTC_Init(RTC_HandleTypeDef *hrtc);
//STATIC void PYB_RTC_MspInit_Kick(RTC_HandleTypeDef *hrtc, bool rtc_use_lse);
//STATIC HAL_StatusTypeDef PYB_RTC_MspInit_Finalise(RTC_HandleTypeDef *hrtc);
//STATIC void RTC_CalendarConfig(void);

//#if defined(MICROPY_HW_RTC_USE_LSE) && MICROPY_HW_RTC_USE_LSE
//STATIC bool rtc_use_lse = true;
//#else
//STATIC bool rtc_use_lse = false;
//#endif
//STATIC uint32_t rtc_startup_tick;
STATIC bool rtc_need_init_finalise = false;

// check if LSE exists
// not well tested, should probably be removed
//STATIC bool lse_magic(void) {
//#if 0
//    uint32_t mode_in = GPIOC->MODER & 0x3fffffff;
//    uint32_t mode_out = mode_in | 0x40000000;
//    GPIOC->MODER = mode_out;
//    GPIOC->OTYPER &= 0x7fff;
//    GPIOC->BSRRH = 0x8000;
//    GPIOC->OSPEEDR &= 0x3fffffff;
//    GPIOC->PUPDR &= 0x3fffffff;
//    int i = 0xff0;
//    __IO int d = 0;
//    uint32_t tc = 0;
//    __IO uint32_t j;
//    while (i) {
//        GPIOC->MODER = mode_out;
//        GPIOC->MODER = mode_in;
//        for (j = 0; j < d; j++) ;
//        i--;
//        if ((GPIOC->IDR & 0x8000) == 0) {
//            tc++;
//        }
//    }
//    return (tc < 0xff0)?true:false;
//#else
//    return false;
//#endif
//}

void rtc_init_start(bool force_init) {
//    RTCHandle.Instance = RTC;

    /* Configure RTC prescaler and RTC data registers */
    /* RTC configured as follow:
      - Hour Format    = Format 24
      - Asynch Prediv  = Value according to source clock
      - Synch Prediv   = Value according to source clock
      - OutPut         = Output Disable
      - OutPutPolarity = High Polarity
      - OutPutType     = Open Drain */
//    RTCHandle.Init.HourFormat = RTC_HOURFORMAT_24;
//    RTCHandle.Init.AsynchPrediv = RTC_ASYNCH_PREDIV;
//    RTCHandle.Init.SynchPrediv = RTC_SYNCH_PREDIV;
//    RTCHandle.Init.OutPut = RTC_OUTPUT_DISABLE;
//    RTCHandle.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
//    RTCHandle.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
//
//    rtc_need_init_finalise = false;
//
//    if (!force_init) {
//        if ((RCC->BDCR & (RCC_BDCR_LSEON | RCC_BDCR_LSERDY)) == (RCC_BDCR_LSEON | RCC_BDCR_LSERDY)) {
//            // LSE is enabled & ready --> no need to (re-)init RTC
//            // remove Backup Domain write protection
//            HAL_PWR_EnableBkUpAccess();
//            // Clear source Reset Flag
//            __HAL_RCC_CLEAR_RESET_FLAGS();
//            // provide some status information
//            rtc_info |= 0x40000 | (RCC->BDCR & 7) | (RCC->CSR & 3) << 8;
//            return;
//        } else if ((RCC->BDCR & RCC_BDCR_RTCSEL) == RCC_BDCR_RTCSEL_1) {
//            // LSI configured as the RTC clock source --> no need to (re-)init RTC
//            // remove Backup Domain write protection
//            HAL_PWR_EnableBkUpAccess();
//            // Clear source Reset Flag
//            __HAL_RCC_CLEAR_RESET_FLAGS();
//            // Turn the LSI on (it may need this even if the RTC is running)
//            RCC->CSR |= RCC_CSR_LSION;
//            // provide some status information
//            rtc_info |= 0x80000 | (RCC->BDCR & 7) | (RCC->CSR & 3) << 8;
//            return;
//        }
//    }
//
//    rtc_startup_tick = HAL_GetTick();
//    rtc_info = 0x3f000000 | (rtc_startup_tick & 0xffffff);
//    if (rtc_use_lse) {
//        if (lse_magic()) {
//            // don't even try LSE
//            rtc_use_lse = false;
//            rtc_info &= ~0x01000000;
//        }
//    }
//    PYB_RTC_MspInit_Kick(&RTCHandle, rtc_use_lse);

        rtc_need_init_finalise = false;
        if(ROM_HibernateIsActive()){
            return;
        }
        ROM_HibernateEnableExpClk(ROM_SysCtlClockGet());
        ROM_HibernateRTCEnable();
        ROM_HibernateRTCTrimSet(0x7FFF);
}

void rtc_init_finalise() {
    if (!rtc_need_init_finalise) {
        return;
    }

//    rtc_info = 0x20000000;
//    if (PYB_RTC_Init(&RTCHandle) != HAL_OK) {
//        if (rtc_use_lse) {
//            // fall back to LSI...
//            rtc_use_lse = false;
//            rtc_startup_tick = HAL_GetTick();
//            PYB_RTC_MspInit_Kick(&RTCHandle, rtc_use_lse);
//            HAL_PWR_EnableBkUpAccess();
//            RTCHandle.State = HAL_RTC_STATE_RESET;
//            if (PYB_RTC_Init(&RTCHandle) != HAL_OK) {
//                rtc_info = 0x0100ffff; // indicate error
//                return;
//            }
//        } else {
//            // init error
//            rtc_info = 0xffff; // indicate error
//            return;
//        }
//    }
//
//    // record if LSE or LSI is used
//    rtc_info |= (rtc_use_lse << 28);
//
//    // record how long it took for the RTC to start up
//    rtc_info |= (HAL_GetTick() - rtc_startup_tick) & 0xffff;
//
//    // fresh reset; configure RTC Calendar
//    RTC_CalendarConfig();
//    #if defined(STM32L4)
//    if(__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) != RESET) {
//    #else
//    if(__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != RESET) {
//    #endif
//        // power on reset occurred
//        rtc_info |= 0x10000;
//    }
//    if(__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != RESET) {
//        // external reset occurred
//        rtc_info |= 0x20000;
//    }
//    // Clear source Reset Flag
//    __HAL_RCC_CLEAR_RESET_FLAGS();
//    rtc_need_init_finalise = false;
}

//STATIC HAL_StatusTypeDef PYB_RCC_OscConfig(RCC_OscInitTypeDef  *RCC_OscInitStruct) {
//    /*------------------------------ LSI Configuration -------------------------*/
//    if ((RCC_OscInitStruct->OscillatorType & RCC_OSCILLATORTYPE_LSI) == RCC_OSCILLATORTYPE_LSI) {
//        // Check the LSI State
//        if (RCC_OscInitStruct->LSIState != RCC_LSI_OFF) {
//            // Enable the Internal Low Speed oscillator (LSI).
//            __HAL_RCC_LSI_ENABLE();
//        } else {
//            // Disable the Internal Low Speed oscillator (LSI).
//            __HAL_RCC_LSI_DISABLE();
//        }
//    }
//
//    /*------------------------------ LSE Configuration -------------------------*/
//    if ((RCC_OscInitStruct->OscillatorType & RCC_OSCILLATORTYPE_LSE) == RCC_OSCILLATORTYPE_LSE) {
//        #if !defined(STM32H7)
//        // Enable Power Clock
//        __HAL_RCC_PWR_CLK_ENABLE();
//        #endif
//
//        // Enable access to the backup domain
//        HAL_PWR_EnableBkUpAccess();
//        uint32_t tickstart = HAL_GetTick();
//
//        #if defined(STM32F7) || defined(STM32L4) || defined(STM32H7)
//        //__HAL_RCC_PWR_CLK_ENABLE();
//        // Enable write access to Backup domain
//        //PWR->CR1 |= PWR_CR1_DBP;
//        // Wait for Backup domain Write protection disable
//        while ((PWR->CR1 & PWR_CR1_DBP) == RESET) {
//            if (HAL_GetTick() - tickstart > RCC_DBP_TIMEOUT_VALUE) {
//                return HAL_TIMEOUT;
//            }
//        }
//        #else
//        // Enable write access to Backup domain
//        //PWR->CR |= PWR_CR_DBP;
//        // Wait for Backup domain Write protection disable
//        while ((PWR->CR & PWR_CR_DBP) == RESET) {
//            if (HAL_GetTick() - tickstart > RCC_DBP_TIMEOUT_VALUE) {
//                return HAL_TIMEOUT;
//            }
//        }
//        #endif
//
//        // Set the new LSE configuration
//        __HAL_RCC_LSE_CONFIG(RCC_OscInitStruct->LSEState);
//    }
//
//    return HAL_OK;
//}

//STATIC HAL_StatusTypeDef PYB_RTC_Init(RTC_HandleTypeDef *hrtc) {
//    // Check the RTC peripheral state
//    if (hrtc == NULL) {
//        return HAL_ERROR;
//    }
//    if (hrtc->State == HAL_RTC_STATE_RESET) {
//        // Allocate lock resource and initialize it
//        hrtc->Lock = HAL_UNLOCKED;
//        // Initialize RTC MSP
//        if (PYB_RTC_MspInit_Finalise(hrtc) != HAL_OK) {
//            return HAL_ERROR;
//        }
//    }
//
//    // Set RTC state
//    hrtc->State = HAL_RTC_STATE_BUSY;
//
//    // Disable the write protection for RTC registers
//    __HAL_RTC_WRITEPROTECTION_DISABLE(hrtc);
//
//    // Set Initialization mode
//    if (RTC_EnterInitMode(hrtc) != HAL_OK) {
//        // Enable the write protection for RTC registers
//        __HAL_RTC_WRITEPROTECTION_ENABLE(hrtc);
//
//        // Set RTC state
//        hrtc->State = HAL_RTC_STATE_ERROR;
//
//        return HAL_ERROR;
//    } else {
//        // Clear RTC_CR FMT, OSEL and POL Bits
//        hrtc->Instance->CR &= ((uint32_t)~(RTC_CR_FMT | RTC_CR_OSEL | RTC_CR_POL));
//        // Set RTC_CR register
//        hrtc->Instance->CR |= (uint32_t)(hrtc->Init.HourFormat | hrtc->Init.OutPut | hrtc->Init.OutPutPolarity);
//
//        // Configure the RTC PRER
//        hrtc->Instance->PRER = (uint32_t)(hrtc->Init.SynchPrediv);
//        hrtc->Instance->PRER |= (uint32_t)(hrtc->Init.AsynchPrediv << 16);
//
//        // Exit Initialization mode
//        hrtc->Instance->ISR &= (uint32_t)~RTC_ISR_INIT;
//
//        #if defined(STM32L4) || defined(STM32H7)
//        hrtc->Instance->OR &= (uint32_t)~RTC_OR_ALARMOUTTYPE;
//        hrtc->Instance->OR |= (uint32_t)(hrtc->Init.OutPutType);
//        #elif defined(STM32F7)
//        hrtc->Instance->OR &= (uint32_t)~RTC_OR_ALARMTYPE;
//        hrtc->Instance->OR |= (uint32_t)(hrtc->Init.OutPutType);
//        #else
//        hrtc->Instance->TAFCR &= (uint32_t)~RTC_TAFCR_ALARMOUTTYPE;
//        hrtc->Instance->TAFCR |= (uint32_t)(hrtc->Init.OutPutType);
//        #endif
//
//        // Enable the write protection for RTC registers
//        __HAL_RTC_WRITEPROTECTION_ENABLE(hrtc);
//
//        // Set RTC state
//        hrtc->State = HAL_RTC_STATE_READY;
//
//        return HAL_OK;
//    }
//}

//STATIC void PYB_RTC_MspInit_Kick(RTC_HandleTypeDef *hrtc, bool rtc_use_lse) {
//    /* To change the source clock of the RTC feature (LSE, LSI), You have to:
//       - Enable the power clock using __PWR_CLK_ENABLE()
//       - Enable write access using HAL_PWR_EnableBkUpAccess() function before to
//         configure the RTC clock source (to be done once after reset).
//       - Reset the Back up Domain using __HAL_RCC_BACKUPRESET_FORCE() and
//         __HAL_RCC_BACKUPRESET_RELEASE().
//       - Configure the needed RTc clock source */
//
//    // RTC clock source uses LSE (external crystal) only if relevant
//    // configuration variable is set.  Otherwise it uses LSI (internal osc).
//
//    RCC_OscInitTypeDef RCC_OscInitStruct;
//    RCC_OscInitStruct.OscillatorType =  RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_LSE;
//    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
//    if (rtc_use_lse) {
//        #if MICROPY_HW_RTC_USE_BYPASS
//        RCC_OscInitStruct.LSEState = RCC_LSE_BYPASS;
//        #else
//        RCC_OscInitStruct.LSEState = RCC_LSE_ON;
//        #endif
//        RCC_OscInitStruct.LSIState = RCC_LSI_OFF;
//    } else {
//        RCC_OscInitStruct.LSEState = RCC_LSE_OFF;
//        RCC_OscInitStruct.LSIState = RCC_LSI_ON;
//    }
//    PYB_RCC_OscConfig(&RCC_OscInitStruct);
//
//    // now ramp up osc. in background and flag calendear init needed
//    rtc_need_init_finalise = true;
//}

#define PYB_LSE_TIMEOUT_VALUE 1000  // ST docs spec 2000 ms LSE startup, seems to be too pessimistic
#define PYB_LSI_TIMEOUT_VALUE 500   // this is way too pessimistic, typ. < 1ms

//STATIC HAL_StatusTypeDef PYB_RTC_MspInit_Finalise(RTC_HandleTypeDef *hrtc) {
//    // we already had a kick so now wait for the corresponding ready state...
//    if (rtc_use_lse) {
//        // we now have to wait for LSE ready or timeout
//        uint32_t tickstart = rtc_startup_tick;
//        while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == RESET) {
//            if ((HAL_GetTick() - tickstart ) > PYB_LSE_TIMEOUT_VALUE) {
//                return HAL_TIMEOUT;
//            }
//        }
//    } else {
//        // we now have to wait for LSI ready or timeout
//        uint32_t tickstart = rtc_startup_tick;
//        while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) == RESET) {
//            if ((HAL_GetTick() - tickstart ) > PYB_LSI_TIMEOUT_VALUE) {
//                return HAL_TIMEOUT;
//            }
//        }
//    }
//
//    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct;
//    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
//    if (rtc_use_lse) {
//        PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
//    } else {
//        PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
//    }
//    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
//        //Error_Handler();
//        return HAL_ERROR;
//    }
//
//    // enable RTC peripheral clock
//    __HAL_RCC_RTC_ENABLE();
//    return HAL_OK;
//}

//STATIC void RTC_CalendarConfig(void) {
//    // set the date to 1st Jan 2015
//    RTC_DateTypeDef date;
//    date.Year = 15;
//    date.Month = 1;
//    date.Date = 1;
//    date.WeekDay = RTC_WEEKDAY_THURSDAY;
//
//    if(HAL_RTC_SetDate(&RTCHandle, &date, RTC_FORMAT_BIN) != HAL_OK) {
//        // init error
//        return;
//    }
//
//    // set the time to 00:00:00
//    RTC_TimeTypeDef time;
//    time.Hours = 0;
//    time.Minutes = 0;
//    time.Seconds = 0;
//    time.TimeFormat = RTC_HOURFORMAT12_AM;
//    time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
//    time.StoreOperation = RTC_STOREOPERATION_RESET;
//
//    if (HAL_RTC_SetTime(&RTCHandle, &time, RTC_FORMAT_BIN) != HAL_OK) {
//        // init error
//        return;
//    }
//}

/******************************************************************************/
// MicroPython bindings

typedef struct _pyb_rtc_obj_t {
    mp_obj_base_t base;
} pyb_rtc_obj_t;

STATIC const pyb_rtc_obj_t pyb_rtc_obj = {{&pyb_rtc_type}};

/// \classmethod \constructor()
/// Create an RTC object.
STATIC mp_obj_t pyb_rtc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 0, 0, false);

    // return constant object
    return (mp_obj_t)&pyb_rtc_obj;
}

// force rtc to re-initialise
mp_obj_t pyb_rtc_init(mp_obj_t self_in) {
    rtc_init_start(true);
    rtc_init_finalise();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(pyb_rtc_init_obj, pyb_rtc_init);

/// \method info()
/// Get information about the startup time and reset source.
///
///  - The lower 0xffff are the number of milliseconds the RTC took to
///    start up.
///  - Bit 0x10000 is set if a power-on reset occurred.
///  - Bit 0x20000 is set if an external reset occurred
mp_obj_t pyb_rtc_info(mp_obj_t self_in) {
    return mp_obj_new_int(rtc_info);
}
MP_DEFINE_CONST_FUN_OBJ_1(pyb_rtc_info_obj, pyb_rtc_info);

/// \method datetime([datetimetuple])
/// Get or set the date and time of the RTC.
///
/// With no arguments, this method returns an 8-tuple with the current
/// date and time.  With 1 argument (being an 8-tuple) it sets the date
/// and time.
///
/// The 8-tuple has the following format:
///
///     (year, month, day, weekday, hours, minutes, seconds, subseconds)
///
/// `weekday` is 1-7 for Monday through Sunday.
///
/// `subseconds` counts down from 255 to 0

#define MEG_MUL (30.501)
#define MEG_DIV (32786)

#if defined(MICROPY_HW_RTC_USE_US) && MICROPY_HW_RTC_USE_US
uint32_t rtc_subsec_to_us(uint32_t ss) {
    return ss * MEG_MUL;
}

uint32_t rtc_us_to_subsec(uint32_t us) {
    return us / MEG_DIV;
}
#else
#define rtc_us_to_subsec
#define rtc_subsec_to_us
#endif

mp_obj_t pyb_rtc_time(size_t n_args, const mp_obj_t *args) {
    rtc_init_finalise();
    if (n_args == 1) {
        // get date and time
        // note: need to call get time then get date to correctly access the registers
        uint32_t time = HibernateRTCGet();
#if defined(MICROPY_HW_RTC_USE_US) && MICROPY_HW_RTC_USE_US
        uint16_t subs = HibernateRTCSSGet();
        mp_obj_t tuple[4] = {
            mp_obj_new_int((time / SEC_HOUR) % 24),
            mp_obj_new_int((time / SEC_MINUTE) % 60),
            mp_obj_new_int(time % 60),
            mp_obj_new_int(rtc_subsec_to_us(subs)),
        };
        return mp_obj_new_tuple(4, tuple);
#else
        mp_obj_t tuple[3] = {
             mp_obj_new_int((time / SEC_HOUR) % 24),
             mp_obj_new_int((time / SEC_MINUTE) % 60),
             mp_obj_new_int(time % 60),
        };
        return mp_obj_new_tuple(3, tuple);
#endif
    } else {
        // set date and time
        mp_obj_t *items;
        mp_obj_get_array_fixed_n(args[1], 4, &items);

        HibernateRTCSet((mp_obj_get_int(items[0]) * SEC_HOUR) + (mp_obj_get_int(items[1]) * SEC_MINUTE) + (mp_obj_get_int(items[2])));
#if defined(MICROPY_HW_RTC_USE_US) && MICROPY_HW_RTC_USE_US
        HibernateRTCSSSet(rtc_us_to_subsec(mp_obj_get_int(items[3]));
#endif
        return mp_const_none;
    }
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_rtc_time_obj, 1, 2, pyb_rtc_time);

// wakeup(None)
// wakeup(ms, callback=None)
// wakeup(wucksel, wut, callback)
mp_obj_t pyb_rtc_wakeup(size_t n_args, const mp_obj_t *args) {
    // wut is wakeup counter start value, wucksel is clock source
    // counter is decremented at wucksel rate, and wakes the MCU when it gets to 0
    // wucksel=0b000 is RTC/16 (RTC runs at 32768Hz)
    // wucksel=0b001 is RTC/8
    // wucksel=0b010 is RTC/4
    // wucksel=0b011 is RTC/2
    // wucksel=0b100 is 1Hz clock
    // wucksel=0b110 is 1Hz clock with 0x10000 added to wut
    // so a 1 second wakeup could be wut=2047, wucksel=0b000, or wut=4095, wucksel=0b001, etc

    rtc_init_finalise();

    // disable wakeup IRQ while we configure it
    ROM_HibernateIntDisable(HIBERNATE_INT_RTC_MATCH_0);
//    HibernateIntDisable(HIBERNATE_INT_GPIO_WAKE | HIBERNATE_INT_LOW_BAT | HIBERNATE_INT_PIN_WAKE | HIBERNATE_INT_RESET_WAKE | HIBERNATE_INT_RTC_MATCH_0 | HIBERNATE_INT_VDDFAIL | HIBERNATE_INT_WR_COMPLETE);

    bool enable = false;
    mp_obj_t callback = mp_const_none;
    if (n_args <= 3) {
        if (args[1] == mp_const_none) {
            // disable wakeup
            ROM_HibernateWakeSet(0x0000);

        } else {
            // time given in ms
            mp_int_t ms = mp_obj_get_int(args[1]);
            HibernateRTCMatchSet(0, ms / 1000);
            HibernateRTCSSMatchSet(0, rtc_us_to_subsec(ms % 1000) * 1000);
            ROM_HibernateWakeSet(HIBERNATE_WAKE_RTC);
            enable = true;
        }
        if (n_args == 2) {
            callback = args[2];
        }
    } else {
        mp_raise_TypeError(MP_ERROR_TEXT("This function only accepts 0 to 2 arguments"));
    }

    // set the callback
    MP_STATE_PORT(pyb_extint_callback)[22] = callback;

    if (enable) {

        ROM_HibernateIntEnable(HIBERNATE_INT_RTC_MATCH_0);
        //printf("wut=%d wucksel=%d\n", wut, wucksel);
    } else {

    }

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_rtc_wakeup_obj, 2, 4, pyb_rtc_wakeup);

// calibration(None)
// calibration(cal)
// When an integer argument is provided, check that it falls in the range [0x0000 to 0xFFFF], nominal 0x7FFF
// and set the calibration value; otherwise return calibration value
mp_obj_t pyb_rtc_calibration(size_t n_args, const mp_obj_t *args) {
    mp_int_t cal;
    if (n_args == 2) {
        cal = mp_obj_get_int(args[1]);
        if (cal < 0xFFFF || cal > 0x0000) {
            ROM_HibernateRTCTrimSet(cal);
        } else {
            mp_raise_ValueError(MP_ERROR_TEXT("calibration value out of range"));
        }
    } else {
        mp_raise_TypeError(MP_ERROR_TEXT("Wrong number of arguments"));
    }
    return mp_obj_new_int(cal);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_rtc_calibration_obj, 1, 2, pyb_rtc_calibration);

STATIC const mp_rom_map_elem_t pyb_rtc_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&pyb_rtc_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_info), MP_ROM_PTR(&pyb_rtc_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_time), MP_ROM_PTR(&pyb_rtc_time_obj) },
    { MP_ROM_QSTR(MP_QSTR_wakeup), MP_ROM_PTR(&pyb_rtc_wakeup_obj) },
    { MP_ROM_QSTR(MP_QSTR_calibration), MP_ROM_PTR(&pyb_rtc_calibration_obj) },
};
STATIC MP_DEFINE_CONST_DICT(pyb_rtc_locals_dict, pyb_rtc_locals_dict_table);

const mp_obj_type_t pyb_rtc_type = {
    { &mp_type_type },
    .name = MP_QSTR_RTC,
    .make_new = pyb_rtc_make_new,
    .locals_dict = (mp_obj_dict_t*)&pyb_rtc_locals_dict,
};
