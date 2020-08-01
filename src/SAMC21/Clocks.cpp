/*
 * Clocks.cpp
 *
 *  Created on: 1 Aug 2020
 *      Author: David
 */

#include <Clocks.h>

void InitStandardClocks(unsigned int xoscFrequency) noexcept
{
	// Set the number of flash wait states
	hri_nvmctrl_set_CTRLB_RWS_bf(NVMCTRL, 2);				// 2 wait states needed at clock frequencies >38MHz

	// 32kHz oscillators
	const uint16_t calib = hri_osc32kctrl_read_OSCULP32K_CALIB_bf(OSC32KCTRL);
	hri_osc32kctrl_write_OSCULP32K_reg(OSC32KCTRL, OSC32KCTRL_OSCULP32K_CALIB(calib));
	hri_osc32kctrl_write_RTCCTRL_reg(OSC32KCTRL, OSC32KCTRL_RTCCTRL_RTCSEL(OSC32KCTRL_RTCCTRL_RTCSEL_ULP32K_Val));

	// Crystal oscillator
	const int32_t gain = (xoscFrequency > 16) ? 4 : 3;		// we are assuming that the frequency is >8MHz so we always need gain at least 3
	hri_oscctrl_write_XOSCCTRL_reg(OSCCTRL,
	    	  OSCCTRL_XOSCCTRL_STARTUP(0)
			| (0 << OSCCTRL_XOSCCTRL_AMPGC_Pos)
	        | OSCCTRL_XOSCCTRL_GAIN(gain)
			| (1 << OSCCTRL_XOSCCTRL_RUNSTDBY_Pos)
	        | (0 << OSCCTRL_XOSCCTRL_SWBEN_Pos)
			| (0 << OSCCTRL_XOSCCTRL_CFDEN_Pos)
	        | (1 << OSCCTRL_XOSCCTRL_XTALEN_Pos)
			| (1 << OSCCTRL_XOSCCTRL_ENABLE_Pos));

	hri_oscctrl_write_EVCTRL_reg(OSCCTRL, (0 << OSCCTRL_EVCTRL_CFDEO_Pos));

	while (!hri_oscctrl_get_STATUS_XOSCRDY_bit(OSCCTRL)) { }
	hri_oscctrl_set_XOSCCTRL_AMPGC_bit(OSCCTRL);

	// DPLL
	// We can divide the crystal oscillator by any even number up to 512 to get an input in the range 32kHz to 2MHz for the DPLL
	// To support all crystal frequencies that are an integral number of MHz, we divide down to 500kHz
	hri_oscctrl_write_DPLLRATIO_reg(OSCCTRL, OSCCTRL_DPLLRATIO_LDRFRAC(0) | OSCCTRL_DPLLRATIO_LDR(95));		// multiply input frequency by 96
	hri_oscctrl_write_DPLLCTRLB_reg(OSCCTRL,
	    	  OSCCTRL_DPLLCTRLB_DIV(xoscFrequency - 1)		// divide by 2 * xoscFrequency to get 0.5MHz
			| (0 << OSCCTRL_DPLLCTRLB_LBYPASS_Pos)
	        | OSCCTRL_DPLLCTRLB_LTIME(0)
			| OSCCTRL_DPLLCTRLB_REFCLK(1)					// reference clock is XOSC
	        | (0 << OSCCTRL_DPLLCTRLB_WUF_Pos)
			| (0 << OSCCTRL_DPLLCTRLB_LPEN_Pos)
	        | OSCCTRL_DPLLCTRLB_FILTER(0));
	hri_oscctrl_write_DPLLPRESC_reg(OSCCTRL, OSCCTRL_DPLLPRESC_PRESC(0));
	hri_oscctrl_write_DPLLCTRLA_reg(OSCCTRL,
			  (0 << OSCCTRL_DPLLCTRLA_RUNSTDBY_Pos)
			| (1 << OSCCTRL_DPLLCTRLA_ENABLE_Pos));
	while (!(hri_oscctrl_get_DPLLSTATUS_LOCK_bit(OSCCTRL) || hri_oscctrl_get_DPLLSTATUS_CLKRDY_bit(OSCCTRL))) { }

	// MCLK
	hri_mclk_write_CPUDIV_reg(MCLK, MCLK_CPUDIV_CPUDIV(1));

	// GCLK 0: 48MHz from DPLL
	hri_gclk_write_GENCTRL_reg(GCLK, 0,
		GCLK_GENCTRL_DIV(1) | (0 << GCLK_GENCTRL_RUNSTDBY_Pos)
			| (0 << GCLK_GENCTRL_DIVSEL_Pos) | (0 << GCLK_GENCTRL_OE_Pos)
			| (0 << GCLK_GENCTRL_OOV_Pos) | (0 << GCLK_GENCTRL_IDC_Pos)
			| GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC_DPLL96M);

	// GCLK 1: 32258Hz (1MHz divided by 31)
	hri_gclk_write_GENCTRL_reg(GCLK, 1,
		GCLK_GENCTRL_DIV(31 * xoscFrequency) | (0 << GCLK_GENCTRL_RUNSTDBY_Pos)
			| (0 << GCLK_GENCTRL_DIVSEL_Pos) | (0 << GCLK_GENCTRL_OE_Pos)
			| (0 << GCLK_GENCTRL_OOV_Pos) | (0 << GCLK_GENCTRL_IDC_Pos)
			| GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC_XOSC);
}

// End
