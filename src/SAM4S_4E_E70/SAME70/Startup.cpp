/**
 * \file
 *
 * \brief GCC startup file for ATSAME70Q21B
 *
 * Copyright (c) 2019 Microchip Technology Inc.
 *
 * \license_start
 *
 * \page License
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * \license_stop
 *
 */

#undef __SOFTFP__			// should not be defined, but Eclipse thinks it is

#include <CoreIO.h>

/* Initialize segments */
extern uint32_t _sfixed;
//extern uint32_t _efixed;
extern uint32_t _etext;
extern uint32_t _srelocate;
extern uint32_t _erelocate;
extern uint32_t _szero;
extern uint32_t _ezero;
//extern uint32_t _sstack;

#if SUPPORT_CAN
extern uint32_t _sCanMessage;
extern uint32_t _eCanMessage;
#endif

extern "C" void __libc_init_array();

// Forward declaration
static void InitClocks() noexcept;

// Definitions for the clock system
// Processor clock is 2 * VARIANT_MCK, maximum 300MHz
// Max PLLA output frequency is 500MHz
#if 1	//VARIANT_MCK == 150000000
const uint32_t PllaMul = 25;			// the multiplier we need from 12MHz to get the required PLLA clock
const uint32_t FlashWaitStates = 6;		// the FWS value needed, one less than the actual number of flash wait states. See table 59-51 of the datasheet.
#elif VARIANT_MCK == 144000000
const uint32_t PllaMul = 24;
const uint32_t FlashWaitStates = 6;
#elif VARIANT_MCK == 120000000
// For this one we could use the 480MHz USB PLL output instead (PMC_MCKR_CSS_UPLL_CLK) and divide it by 2 (PMC_MCKR_PRES_CLK_2)
const uint32_t PllaMul = 20;
const uint32_t FlashWaitStates = 5;
#endif

#define SYS_BOARD_OSCOUNT   (CKGR_MOR_MOSCXTST(0x8U))									// crystal oscillator startup time is 8 * 8 slow clocks
#define SYS_BOARD_PLLAR     (CKGR_PLLAR_ONE | CKGR_PLLAR_MULA(PllaMul - 1) | \
                            CKGR_PLLAR_PLLACOUNT(0x3fU) | CKGR_PLLAR_DIVA_BYPASS)
#define SYS_BOARD_MCKR      (PMC_MCKR_PRES_CLK_1 | PMC_MCKR_CSS_PLLA_CLK | PMC_MCKR_MDIV_PCK_DIV2)

/**
 * \brief This is the code that gets called on processor reset.
 * To initialize the device, and call the main() routine.
 */
extern "C" [[noreturn]] void Reset_Handler() noexcept
{
	uint32_t *pSrc = &_etext;
	uint32_t *pDest = &_srelocate;

	if (pSrc != pDest)
	{
		for (; pDest < &_erelocate; )
		{
			*pDest++ = *pSrc++;
		}
	}

	// Clear the zero segment
	for (pDest = &_szero; pDest < &_ezero; )
	{
		*pDest++ = 0;
	}

#if SUPPORT_CAN
	// Clear the CAN message buffer segment
	for (pDest = &_sCanMessage; pDest < &_eCanMessage; )
	{
		*pDest++ = 0;
	}
#endif

	/* Set the vector table base address */
	pSrc = (uint32_t *) & _sfixed;
	SCB->VTOR = ((uint32_t) pSrc & SCB_VTOR_TBLOFF_Msk);

	// Enable FPU
	static_assert(__FPU_USED);
	SCB->CPACR |= (0xFu << 20);
	__DSB();
	__ISB();

	/* Initialize the C library */
	__libc_init_array();

	// Set up the standard clocks
	InitClocks();

	// Temporarily set up systick so that delayMicroseconds works
	SysTick->LOAD = ((SystemCoreClockFreq/1000) - 1) << SysTick_LOAD_RELOAD_Pos;
	SysTick->CTRL = (1 << SysTick_CTRL_ENABLE_Pos) | (1 << SysTick_CTRL_CLKSOURCE_Pos);

	// Initialise application, which includes setting up any additional clocks
	AppInit();

	// Run the application
	AppMain();

	/* Infinite loop */
	while (1);
}

void InitClocks() noexcept
{
	// Set FWS according to SYS_BOARD_MCKR configuration
	EFC->EEFC_FMR = EEFC_FMR_FWS(FlashWaitStates);

	// Initialize crystal oscillator
	if (!(PMC->CKGR_MOR & CKGR_MOR_MOSCSEL))				// if the crystal oscillator is not selected
	{
		PMC->CKGR_MOR = CKGR_MOR_KEY_PASSWD | SYS_BOARD_OSCOUNT | CKGR_MOR_MOSCRCEN | CKGR_MOR_MOSCXTEN;		// enable the crystal oscillator, keep the RC oscillator enabled
		while (!(PMC->PMC_SR & PMC_SR_MOSCXTS)) { }			// wait until crystal oscillator has stablised
	}

	// Switch to crystal oscillator
	PMC->CKGR_MOR = CKGR_MOR_KEY_PASSWD | SYS_BOARD_OSCOUNT | CKGR_MOR_MOSCRCEN | CKGR_MOR_MOSCXTEN | CKGR_MOR_MOSCSEL;	// switch to crystal oscillator
	while (!(PMC->PMC_SR & PMC_SR_MOSCSELS)) { }			// wait until selection is complete

	// Switch master clock to main clock
	PMC->PMC_MCKR = (PMC->PMC_MCKR & ~(uint32_t)PMC_MCKR_CSS_Msk) | PMC_MCKR_CSS_MAIN_CLK;
	while (!(PMC->PMC_SR & PMC_SR_MCKRDY)) { }

	// Stop the PLL before we change it
	PMC->CKGR_PLLAR = CKGR_PLLAR_ONE | CKGR_PLLAR_MULA(0);

	// Initialize PLLA
	PMC->CKGR_PLLAR = SYS_BOARD_PLLAR;
	while (!(PMC->PMC_SR & PMC_SR_LOCKA)) { }

	// Switch to main clock
	PMC->PMC_MCKR = (SYS_BOARD_MCKR & ~PMC_MCKR_CSS_Msk) | PMC_MCKR_CSS_MAIN_CLK;
	while (!(PMC->PMC_SR & PMC_SR_MCKRDY)) { }

	// Switch to PLLA
	PMC->PMC_MCKR = SYS_BOARD_MCKR;
	while (!(PMC->PMC_SR & PMC_SR_MCKRDY)) { }

	SystemCoreClock = SystemCoreClockFreq;
}

// End
