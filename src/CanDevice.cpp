/*
 * CanDevice.cpp
 *
 *  Created on: 2 Sep 2020
 *      Author: David
 */

#include "CanDevice.h"

#if SUPPORT_CAN

#include <Cache.h>
#include <CanSettings.h>
#include <CanMessageBuffer.h>
#include <CanId.h>
#include <General/Bitmap.h>
#include <cstring>

#if SAME5x
# include <hri_gclk_e54.h>
#elif SAME70
# include <asf/sam/drivers/pmc/pmc.h>
// The following definitions are missing from the MCAN peripheral definition in ASF3
# define MCAN_RXF0C_F0OM_Pos	(31)								/**< (MCAN_RXF0C) FIFO 0 Operation Mode Position */
# define MCAN_RXF1C_F1OM_Pos	(31)								/**< (MCAN_RXF1C) FIFO 1 Operation Mode Position */
# define MCAN_TXBC_TFQM_Pos		(30)								/**< (MCAN_TXBC) Tx FIFO/Queue Mode Position */
# define MCAN_TXBC_TFQM_Msk		(0x1u << MCAN_TXBC_TFQM_Pos)		/**< (MCAN_TXBC) Tx FIFO/Queue Mode Mask */
# define MCAN_TXFQS_TFQF_Pos	(21)								/**< (MCAN_TXFQS) Tx FIFO/Queue Full Position */
# define MCAN_TXFQS_TFQF_Msk	(0x1u << MCAN_TXFQS_TFQF_Pos)		/**< (MCAN_TXFQS) Tx FIFO/Queue Full Mask */
#elif SAMC21
# include <hri_gclk_c21.h>
#else
# error Unsupported processor
#endif

/**@}*/
/**
 * \brief CAN receive FIFO element.
 */
struct CanDevice::CanRxBufferHeader
{
	__IO union
	{
		struct
		{
			uint32_t ID : 29; /*!< Identifier */
			uint32_t RTR : 1; /*!< Remote Transmission Request */
			uint32_t XTD : 1; /*!< Extended Identifier */
			uint32_t ESI : 1; /*!< Error State Indicator */
		} bit;
		uint32_t val; /*!< Type used for register access */
	} R0;
	__IO union
	{
		struct
		{
			uint32_t RXTS : 16; /*!< Rx Timestamp */
			uint32_t DLC : 4;   /*!< Data Length Code */
			uint32_t BRS : 1;   /*!< Bit Rate Switch */
			uint32_t FDF : 1;   /*!< FD Format */
			uint32_t : 2;       /*!< Reserved */
			uint32_t FIDX : 7;  /*!< Filter Index */
			uint32_t ANMF : 1;  /*!< Accepted Non-matching Frame */
		} bit;
		uint32_t val; /*!< Type used for register access */
	} R1;

	const volatile uint32_t *GetDataPointer() const volatile { return (volatile uint32_t*)this + (sizeof(*this)/sizeof(uint32_t)); }
};

template<size_t DataLength> struct CanRxBufferEntry : public CanDevice::CanRxBufferHeader
{
	uint8_t data[DataLength];
};

/**
 * \brief CAN transmit FIFO element.
 */
struct CanDevice::CanTxBufferHeader
{
	__IO union
	{
		struct
		{
			uint32_t ID : 29; /*!< Identifier */
			uint32_t RTR : 1; /*!< Remote Transmission Request */
			uint32_t XTD : 1; /*!< Extended Identifier */
			uint32_t ESI : 1; /*!< Error State Indicator */
		} bit;
		uint32_t val; /*!< Type used for register access */
	} T0;
	__IO union
	{
		struct
		{
			uint32_t : 16;    /*!< Reserved */
			uint32_t DLC : 4; /*!< Data Length Code */
			uint32_t BRS : 1; /*!< Bit Rate Switch */
			uint32_t FDF : 1; /*!< FD Format */
			uint32_t : 1;     /*!< Reserved */
			uint32_t EFCbit : 1; /*!< Event FIFO Control */
			uint32_t MM : 8;  /*!< Message Marker */
		} bit;
		uint32_t val; /*!< Type used for register access */
	} T1;

	volatile uint32_t *GetDataPointer() volatile { return (volatile uint32_t*)this + (sizeof(*this)/sizeof(uint32_t)); }
};

template<size_t DataLength> struct CanTxBufferEntry : public CanDevice::CanTxBufferHeader
{
	uint8_t data[DataLength];
};

/**
 * \brief CAN transmit Event element.
 */
struct CanDevice::CanTxEventEntry
{
	__IO union
	{
		struct
		{
			uint32_t ID : 29; /*!< Identifier */
			uint32_t RTR : 1; /*!< Remote Transmission Request */
			uint32_t XTD : 1; /*!< Extended Identifier */
			uint32_t ESI : 1; /*!< Error State Indicator */
		} bit;
		uint32_t val; /*!< Type used for register access */
	} R0;
	__IO union
	{
		struct
		{
			uint32_t TXTS : 16; /*!< Tx Timestamp */
			uint32_t DLC : 4;   /*!< Data Length Code */
			uint32_t BRS : 1;   /*!< Bit Rate Switch */
			uint32_t FDF : 1;   /*!< FD Format */
			uint32_t ET : 2;    /*!< Event Type */
			uint32_t MM : 8;    /*!< Message Marker */
		} bit;
		uint32_t val; /*!< Type used for register access */
	} R1;
};

/**
 * \brief CAN standard message ID filter element structure.
 *
 *  Common element structure for standard message ID filter element.
 */
struct CanDevice::CanStandardMessageFilterElement
{
	union S0Type
	{
		struct
		{
			uint32_t SFID2 : 11; /*!< Standard Filter ID 2 */
			uint32_t : 5;        /*!< Reserved */
			uint32_t SFID1 : 11; /*!< Standard Filter ID 1 */
			uint32_t SFEC : 3;   /*!< Standard Filter Configuration */
			uint32_t SFT : 2;    /*!< Standard Filter Type */
		} bit;
		uint32_t val; /*!< Type used for register access */
	};

	__IO S0Type S0;
};

/**
 * \brief CAN extended message ID filter element structure.
 *
 *  Common element structure for extended message ID filter element.
 */
struct CanDevice::CanExtendedMessageFilterElement
{
	union F0Type
	{
		struct
		{
			uint32_t EFID1 : 29;	//!< bit: Extended Filter ID 1
			uint32_t EFEC : 3;		//!< bit: Extended Filter Configuration
		} bit;
		uint32_t val;				//!< Type used for register access
	};

	union F1Type
	{
		struct
		{
			uint32_t EFID2 : 29;	//!< bit: Extended Filter ID 2
			uint32_t : 1;			//!< bit: Reserved
			uint32_t EFT : 2;		//!< bit: Extended Filter Type
		} bit;
		uint32_t val;				//!< Type used for register access
	};

	__IO union F0Type F0;
	__IO union F1Type F1;
};

// Macros to handle the differing naming of registers and fields between the SAME70 and the SAME5x/C21
#if SAME5x || SAMC21

# define REG(_x)					_x.reg
# define CAN_(_x)					CAN_ ## _x
# define READBITS(_hw,_x,_y)		(hw)->_x.bit._y
# define WRITEBITS(_hw,_x,_y,_val)	(hw)->_x.bit._y = _val

#elif SAME70

# define REG(_x)					MCAN_ ## _x
# define CAN_(_x)					MCAN_ ## _x
# define READBITS(_hw,_x,_y)		((((hw)->MCAN_ ## _x) & (MCAN_ ## _x ## _ ## _y ## _Msk)) >> (MCAN_ ## _x ## _ ## _y ## _Pos))
# define WRITEBITS(_hw,_x,_y,_val)	(hw)->MCAN_ ## _x = (((hw)->MCAN_ ## _x) & ~(MCAN_ ## _x ## _ ## _y ## _Msk)) | ((_val << (MCAN_ ## _x ## _ ## _y ## _Pos)) & (MCAN_ ## _x ## _ ## _y ## _Msk))

# define CAN0						MCAN0
# define CAN1						MCAN1
# define CAN0_IRQn					MCAN0_INT0_IRQn
# define CAN1_IRQn					MCAN1_INT0_IRQn
# define CAN0_Handler				MCAN0_INT0_Handler
# define CAN1_Handler				MCAN1_INT0_Handler

#endif

static Can * const CanPorts[2] = { CAN0, CAN1 };
static const IRQn IRQnsByPort[2] = { CAN0_IRQn, CAN1_IRQn };
static CanDevice *devicesByPort[2] = { nullptr, nullptr };

CanDevice CanDevice::devices[NumCanDevices];

inline uint32_t CanDevice::GetRxBufferSize() const noexcept { return sizeof(CanRxBufferHeader)/sizeof(uint32_t) + (config->dataSize >> 2); }
inline uint32_t CanDevice::GetTxBufferSize() const noexcept { return sizeof(CanTxBufferHeader)/sizeof(uint32_t) + (config->dataSize >> 2); }
inline volatile CanDevice::CanRxBufferHeader *CanDevice::GetRxFifo0Buffer(uint32_t index) const noexcept { return (volatile CanRxBufferHeader*)(rx0Fifo + (index * GetRxBufferSize())); }
inline volatile CanDevice::CanRxBufferHeader *CanDevice::GetRxFifo1Buffer(uint32_t index) const noexcept { return (volatile CanRxBufferHeader*)(rx1Fifo + (index * GetRxBufferSize())); }
inline volatile CanDevice::CanRxBufferHeader *CanDevice::GetRxBuffer(uint32_t index) const noexcept { return (volatile CanRxBufferHeader*)(rxBuffers + (index * GetRxBufferSize())); }
inline CanDevice::CanTxBufferHeader *CanDevice::GetTxBuffer(uint32_t index) const noexcept { return (CanTxBufferHeader*)(txBuffers + (index * GetTxBufferSize())); }

// Initialise a CAN device and return a pointer to it
/*static*/ CanDevice* CanDevice::Init(unsigned int p_whichCan, unsigned int p_whichPort, const Config& p_config, uint32_t *memStart, const CanTiming &timing) noexcept
{
	if (   p_whichCan >= NumCanDevices									// device number out of range
		|| p_whichPort >= 2												// CAN instance number out of range
		|| devicesByPort[p_whichPort] != nullptr						// CAN instance number already in use
	   )
	{
		return nullptr;
	}

	CanDevice& dev = devices[p_whichCan];
	if (dev.hw != nullptr)												// device instance already in use
	{
		return nullptr;
	}

	// Set up device number, peripheral number, hardware address etc.
	dev.whichCan = p_whichCan;
	dev.whichPort = p_whichPort;
	dev.hw = CanPorts[p_whichPort];
	dev.config = &p_config;
	devicesByPort[p_whichPort] = &dev;

	// Set up pointers to the individual parts of the buffer memory
	memset(memStart, 0, p_config.GetMemorySize());						// clear out filters, transmit pending flags etc.
	dev.rxStdFilter = (CanStandardMessageFilterElement*)memStart;
	memStart += p_config.GetStandardFiltersMemSize();
	dev.rxExtFilter = (CanExtendedMessageFilterElement*)memStart;
	memStart += p_config.GetExtendedFiltersMemSize();
	dev.rx0Fifo = memStart;
	memStart += p_config.rxFifo0Size * p_config.GetRxBufferSize();
	dev.rx1Fifo = memStart;
	memStart += p_config.rxFifo1Size * p_config.GetRxBufferSize();
	dev.rxBuffers = memStart;
	memStart += p_config.numRxBuffers * p_config.GetRxBufferSize();
	dev.txEventFifo = (CanTxEventEntry*)memStart;
	memStart += p_config.GetTxEventFifoMemSize();
	dev.txBuffers = memStart;

	dev.useFDMode = (p_config.dataSize > 8);								// assume we want standard CAN if the max data size is 8
	dev.messagesQueuedForSending = dev.messagesReceived = dev.messagesLost = dev.busOffCount = dev.txTimeouts = 0;
#ifdef RTOS
	dev.rxBuffersWaiting.Clear();
	dev.txBuffersWaiting.Clear();
#endif

	dev.UpdateLocalCanTiming(timing);									// sets NBTP and DBTP

	// Enable the clock
#if SAME5x || SAMC21
	if (p_whichPort == 0)
	{
		MCLK->AHBMASK.reg |= MCLK_AHBMASK_CAN0;
		hri_gclk_write_PCHCTRL_reg(GCLK, CAN0_GCLK_ID, GclkNum48MHz | GCLK_PCHCTRL_CHEN);
	}
	else
	{
		MCLK->AHBMASK.reg |= MCLK_AHBMASK_CAN1;
		hri_gclk_write_PCHCTRL_reg(GCLK, CAN1_GCLK_ID, GclkNum48MHz | GCLK_PCHCTRL_CHEN);
	}
#elif SAME70
	if (p_whichPort == 0)
	{
		pmc_enable_periph_clk(ID_MCAN0);
	}
	else
	{
		pmc_enable_periph_clk(ID_MCAN1);
	}
#endif

	dev.DoHardwareInit();
	return &dev;
}

// Do the low level hardware initialisation
void CanDevice::DoHardwareInit() noexcept
{
	Disable();

	if (useFDMode)
	{
		hw->REG(CCCR) |= CAN_(CCCR_FDOE) | CAN_(CCCR_BRSE);
	}
	else
	{
		hw->REG(CCCR) &= ~(CAN_(CCCR_FDOE) | CAN_(CCCR_BRSE));
	}
#if SAME5x || SAMC21
	hw->MRCFG.reg = CAN_MRCFG_QOS_MEDIUM;
#endif
#if SAME70
	// Set upper 16 bits of DMA addresses
	if (whichCan == 0)
	{
		MATRIX->CCFG_CAN0 = (MATRIX->CCFG_CAN0 & 0x0000FFFF) | ((uint32_t)rxBuffers & 0xFFFF0000);
	}
	else
	{
		MATRIX->CCFG_SYSIO = (MATRIX->CCFG_SYSIO & 0x0000FFFF) | ((uint32_t)rxBuffers & 0xFFFF0000);
	}
#endif
	hw->REG(TDCR) = 0;														// use just the measured transceiver delay
	hw->REG(NBTP) = nbtp;
	hw->REG(DBTP) = dbtp;
	hw->REG(RXF0C) = 														// configure receive FIFO 0
		  (0 << CAN_(RXF0C_F0OM_Pos))										// blocking mode not overwrite mode
		| CAN_(RXF0C_F0WM)(0)												// no watermark interrupt
		| CAN_(RXF0C_F0S)(config->rxFifo0Size)								// number of entries
		| CAN_(RXF0C_F0SA)((uint32_t)rx0Fifo);								// address
	hw->REG(RXF1C) = 														// configure receive FIFO 1
		  (0 << CAN_(RXF1C_F1OM_Pos))										// blocking mode not overwrite mode
		| CAN_(RXF1C_F1WM)(0)												// no watermark interrupt
		| CAN_(RXF1C_F1S)(config->rxFifo1Size)								// number of entries
		| CAN_(RXF1C_F1SA)((uint32_t)rx1Fifo);								// address
	hw->REG(RXBC) = CAN_(RXBC_RBSA)((uint32_t)rxBuffers);					// dedicated buffers start address

	const uint32_t dataSizeCode = (config->dataSize <= 24) ? (config->dataSize >> 2) - 2 : (config->dataSize >> 4) + 3;
	hw->REG(RXESC) = CAN_(RXESC_F0DS)(dataSizeCode) | CAN_(RXESC_F1DS)(dataSizeCode) | CAN_(RXESC_RBDS)(dataSizeCode);	// receive buffer and fifo data size
	hw->REG(TXESC) = CAN_(TXESC_TBDS)(dataSizeCode);						// transmit buffer data size
	hw->REG(TXBC) = 														// configure transmit buffers
		  (0 << CAN_(TXBC_TFQM_Pos))										// FIFO not queue
		| CAN_(TXBC_TFQS)(config->txFifoSize + config->numTxBuffers)		// number of Tx buffer entries
		| CAN_(TXBC_TBSA)((uint32_t)txBuffers);								// address
	hw->REG(TXEFC) =  														// configure Tx event fifo
		  CAN_(TXEFC_EFWM)(0)
		| CAN_(TXEFC_EFS)(config->txEventFifoSize)
		| CAN_(TXEFC_EFSA)((uint32_t)txEventFifo);							// address
	hw->REG(GFC) =
#if SAME70
		  MCAN_GFC_ANFE(2)
		| MCAN_GFC_ANFS(2)
#else
		  CAN_(GFC_ANFS_REJECT)
		| CAN_(GFC_ANFE_REJECT)
#endif
		| CAN_(GFC_RRFS)
		| CAN_(GFC_RRFE);
	hw->REG(SIDFC) = CAN_(SIDFC_LSS)(config->numShortFilterElements) | CAN_(SIDFC_FLSSA)((uint32_t)rxStdFilter);
	hw->REG(XIDFC) = CAN_(XIDFC_LSE)(config->numExtendedFilterElements) | CAN_(XIDFC_FLESA)((uint32_t)rxExtFilter);
	hw->REG(XIDAM) = CAN_(XIDAM_EIDM)(0x1FFFFFFF);

	hw->REG(IR) = 0xFFFFFFFF;												// clear all interrupt sources

#ifdef RTOS
	const IRQn irqn = IRQnsByPort[whichPort];
	NVIC_DisableIRQ(irqn);
	NVIC_ClearPendingIRQ(irqn);

	hw->REG(ILS) = 0;														// all interrupt sources assigned to interrupt line 0 for now
	hw->REG(IE) = CAN_(IE_RF0NE) | CAN_(IE_RF1NE) | CAN_(IE_DRXE) | CAN_(IE_TCE) | CAN_(IE_BOE) | CAN_(IE_RF0LE) | CAN_(IE_RF1LE);	// enable the interrupt sources that we want
	hw->REG(ILE) = CAN_(ILE_EINT0);											// enable interrupt line 0

	NVIC_EnableIRQ(irqn);
#else
	hw->REG(IE) = 0;														// disable all interrupt sources
	hw->REG(ILE) = 0;
#endif
	// Leave the device disabled. Client must call Enable() to enable it after setting up the receive filters.
}

// Stop and free this device and the CAN port it uses
void CanDevice::DeInit() noexcept
{
	if (hw != nullptr)
	{
		Disable();
		NVIC_DisableIRQ(IRQnsByPort[whichPort]);
		devicesByPort[whichPort] = nullptr;									// free the port
		hw = nullptr;														// free the device
	}
}

// Enable this device
void CanDevice::Enable() noexcept
{
	hw->REG(CCCR) &= ~CAN_(CCCR_INIT);
	while ((hw->REG(CCCR) & CAN_(CCCR_INIT)) != 0) { }
}

// Disable this device
void CanDevice::Disable() noexcept
{
	hw->REG(CCCR) |= CAN_(CCCR_INIT);
	while ((hw->REG(CCCR) & CAN_(CCCR_INIT)) == 0) { }
	hw->REG(CCCR) |= CAN_(CCCR_CCE);
}

// Return true if space is available to send using this buffer or FIFO
bool CanDevice::IsSpaceAvailable(TxBufferNumber whichBuffer, uint32_t timeout) noexcept
{
#ifndef RTOS
	const uint32_t start = millis();
#endif

	bool bufferFree;
	if (whichBuffer == TxBufferNumber::fifo)
	{
#ifdef RTOS
		bufferFree = (READBITS(hw, TXFQS, TFQF) == 0);
		if (!bufferFree && timeout != 0)
		{
			TaskBase::ClearNotifyCount();
			txTaskWaiting[(unsigned int)whichBuffer] = TaskBase::GetCallerTaskHandle();
			txBuffersWaiting.SetBit((unsigned int)whichBuffer);
			bufferFree = (READBITS(hw, TXFQS, TFQF) == 0);
			if (!bufferFree)
			{
				TaskBase::Take(timeout);
				bufferFree = (READBITS(hw, TXFQS, TFQF) == 0);
			}
			txBuffersWaiting.ClearBit((unsigned int)whichBuffer);
		}
#else
		do
		{
			bufferFree = READBITS(hw, TXFQS, TFQF) == 0;
		} while (!bufferFree && millis() - start < timeout);
#endif
	}
	else
	{
		const unsigned int bufferNumber = (unsigned int)whichBuffer - (unsigned int)TxBufferNumber::buffer0;
#ifdef RTOS
		bufferFree = (hw->REG(TXBRP) & ((uint32_t)1 << bufferNumber)) == 0;
		if (!bufferFree && timeout != 0)
		{
			TaskBase::ClearNotifyCount();
			txTaskWaiting[(unsigned int)whichBuffer] = TaskBase::GetCallerTaskHandle();
			txBuffersWaiting.SetBit((unsigned int)whichBuffer);
			hw->REG(TXBTIE) |= (uint32_t)1 << bufferNumber;
			bufferFree = (hw->REG(TXBRP) & ((uint32_t)1 << bufferNumber)) == 0;
			if (!bufferFree)
			{
				TaskBase::Take(timeout);
				bufferFree = (hw->REG(TXBRP) & ((uint32_t)1 << bufferNumber)) == 0;
			}
			txBuffersWaiting.ClearBit((unsigned int)whichBuffer);
			hw->REG(TXBTIE) &= ~((uint32_t)1 << bufferNumber);
		}
#else
		do
		{
			bufferFree = (hw->REG(TXBRP) & ((uint32_t)1 << bufferNumber)) == 0;
		} while (!bufferFree && millis() - start < timeout);
#endif
	}
	return bufferFree;
}

void CanDevice::CopyMessageForTransmit(CanMessageBuffer *buffer, volatile CanTxBufferHeader *f) noexcept
{
	if (buffer->extId)
	{
		f->T0.val = buffer->id.GetWholeId();
		f->T0.bit.XTD = 1;
	}
	else
	{
		/* A standard identifier is stored into ID[28:18] */
		f->T0.val = buffer->id.GetWholeId() << 18;
		f->T0.bit.XTD = 0;
	}

	f->T1.bit.EFCbit = 0;
	uint32_t dataLength = buffer->dataLength;
	volatile uint32_t *dataPtr = f->GetDataPointer();
	if (dataLength <= 8)
	{
		f->T1.bit.DLC = dataLength;
		dataPtr[0] = buffer->msg.raw32[0];
		dataPtr[1] = buffer->msg.raw32[1];
	}
	else
	{
		while (dataLength & 3)
		{
			buffer->msg.raw[dataLength++] = 0;				// pad length to a multiple of 4 bytes, setting any additional bytes we send to zero in case the message ends with a string
		}

		if (dataLength <= 24)
		{
			// DLC values 9, 10, 11, 12 code for lengths 12, 16, 20, 24
			uint8_t dlc = (dataLength >> 2) + 6;
			f->T1.bit.DLC = dlc;
			const uint32_t *p = buffer->msg.raw32;
			do
			{
				*dataPtr++ = *p++;
				--dlc;
			} while (dlc != 6);								// copy 3, 4, 5 or 6 words
		}
		else
		{
			// DLC values 13, 14, 15 code for lengths 32, 48, 64
			while (dataLength & 12)
			{
				buffer->msg.raw32[dataLength >> 2] = 0;		// pad length to a multiple of 16 bytes, setting any additional bytes we send to zero in case the message ends with a string
				dataLength += 4;
			}

			uint8_t dlc = (dataLength >> 4) + 11;
			f->T1.bit.DLC = dlc;
			const uint32_t *p = buffer->msg.raw32;
			do
			{
				*dataPtr++ = *p++;
				*dataPtr++ = *p++;
				*dataPtr++ = *p++;
				*dataPtr++ = *p++;
				--dlc;
			} while (dlc != 11);
		}
	}

	f->T1.bit.FDF = buffer->fdMode;
	f->T1.bit.BRS = buffer->useBrs;

	++messagesQueuedForSending;
}

// Queue a message for sending via a buffer or FIFO. If the buffer isn't free, cancel the previous message (or oldest message in the fifo) and send it anyway.
// On return the caller must free or re-use the buffer.
void CanDevice::SendMessage(TxBufferNumber whichBuffer, uint32_t timeout, CanMessageBuffer *buffer) noexcept
{
	const bool bufferFree = IsSpaceAvailable(whichBuffer, timeout);
	if (whichBuffer == TxBufferNumber::fifo)
	{
		const uint32_t putIndex = (hw->REG(TXFQS) & CAN_(TXFQS_TFQPI_Msk)) >> CAN_(TXFQS_TFQPI_Pos);
		const uint32_t trigMask = (uint32_t)1 << putIndex;
		if (!bufferFree)
		{
			// Cancel transmission of the oldest packet
			hw->REG(TXBCR) = trigMask;
			do
			{
				delay(1);
			}
			while ((hw->REG(TXBRP) & trigMask) != 0 || READBITS(hw, TXFQS, TFQF));
			++txTimeouts;
		}

		CopyMessageForTransmit(buffer, GetTxBuffer(putIndex));
		hw->REG(TXBAR) = trigMask;
	}
	else if ((uint32_t)whichBuffer < (uint32_t)TxBufferNumber::buffer0 + config->numTxBuffers)
	{
		const uint32_t bufferIndex = (uint32_t)whichBuffer - (uint32_t)TxBufferNumber::buffer0;
		const uint32_t trigMask = (uint32_t)1 << bufferIndex;
		if (!bufferFree)
		{
			// Cancel transmission of the existing packet in this buffer
			hw->REG(TXBCR) = trigMask;
			do
			{
				delay(1);
			}
			while ((hw->REG(TXBRP) & trigMask) != 0);
			++txTimeouts;
		}
		CopyMessageForTransmit(buffer, GetTxBuffer(bufferIndex));
		hw->REG(TXBAR) = trigMask;
	}
}

void CanDevice::CopyReceivedMessage(CanMessageBuffer *buffer, const volatile CanRxBufferHeader *f) noexcept
{
	// The CAN has written the message directly to memory, so we must invalidate the cache before we read it
	Cache::InvalidateAfterDMAReceive(f, sizeof(CanRxBufferHeader) + 64);					// flush the header and up to 64 bytes of data

	buffer->extId = f->R0.bit.XTD;
	buffer->id.SetReceivedId((buffer->extId) ? f->R0.bit.ID : f->R0.bit.ID >> 18);			// a standard identifier is stored into ID[28:18]
	buffer->remote = f->R0.bit.RTR;

	const volatile uint32_t *data = f->GetDataPointer();
	uint8_t dlc = f->R1.bit.DLC;
	static constexpr uint8_t dlc2len[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};

	switch (dlc)
	{
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
		buffer->msg.raw32[1] = data[1];
		// no break
	case 0:
	case 1:
	case 2:
	case 3:
		buffer->msg.raw32[0] = data[0];
		buffer->dataLength = dlc;
		break;

	case 15:		// 64 bytes
		buffer->msg.raw32[12] = data[12];
		buffer->msg.raw32[13] = data[13];
		buffer->msg.raw32[14] = data[14];
		buffer->msg.raw32[15] = data[15];
		// no break
	case 14:		// 48 bytes
		buffer->msg.raw32[8] = data[8];
		buffer->msg.raw32[9] = data[9];
		buffer->msg.raw32[10] = data[10];
		buffer->msg.raw32[11] = data[11];
		// no break
	case 13:		// 32 bytes
		buffer->msg.raw32[6] = data[6];
		buffer->msg.raw32[7] = data[7];
		// no break
	case 12:		// 24 bytes
		buffer->msg.raw32[5] = data[5];
		// no break
	case 11:		// 20 bytes
		buffer->msg.raw32[4] = data[4];
		// no break
	case 10:		// 16 bytes
		buffer->msg.raw32[3] = data[3];
		// no break
	case 9:			// 12 bytes
		buffer->msg.raw32[0] = data[0];
		buffer->msg.raw32[1] = data[1];
		buffer->msg.raw32[2] = data[2];
		buffer->dataLength = dlc2len[f->R1.bit.DLC];
	}

	++messagesReceived;
}

// Receive a message in a buffer or fifo, with timeout. Returns true if successful, false if no message available even after the timeout period.
bool CanDevice::ReceiveMessage(RxBufferNumber whichBuffer, uint32_t timeout, CanMessageBuffer *buffer) noexcept
{
#ifndef RTOS
	const uint32_t start = millis();
#endif

	switch (whichBuffer)
	{
	case RxBufferNumber::fifo0:
		{
			// Check for a received message and wait if necessary
#ifdef RTOS
			if (READBITS(hw, RXF0S, F0FL) == 0)
			{
				if (timeout == 0)
				{
					return false;
				}
				TaskBase::ClearNotifyCount();
				const unsigned int waitingIndex = (unsigned int)whichBuffer;
				rxTaskWaiting[waitingIndex] = TaskBase::GetCallerTaskHandle();
				rxBuffersWaiting.SetBit(waitingIndex);
				const bool success = (READBITS(hw, RXF0S, F0FL) != 0) || (TaskBase::Take(timeout), READBITS(hw, RXF0S, F0FL) != 0);
				rxBuffersWaiting.ClearBit(waitingIndex);
				if (!success)
				{
					return false;
				}
			}
#else
			while (READBITS(hw, RXF0S, F0FL) == 0)
			{
				if (millis() - start >= timeout)
				{
					return false;
				}
			}
#endif
			// Process the received message into the buffer
			const uint32_t getIndex = (hw->REG(RXF0S) & CAN_(RXF0S_F0GI_Msk)) >> CAN_(RXF0S_F0GI_Pos);
			CopyReceivedMessage(buffer, GetRxFifo0Buffer(getIndex));

			// Tell the hardware that we have taken the message
			WRITEBITS(hw, RXF0A, F0AI, getIndex);
		}
		return true;

	case RxBufferNumber::fifo1:
		// Check for a received message and wait if necessary
		{
#ifdef RTOS
			if (READBITS(hw, RXF1S, F1FL) == 0)
			{
				if (timeout == 0)
				{
					return false;
				}
				TaskBase::ClearNotifyCount();
				const unsigned int waitingIndex = (unsigned int)whichBuffer;
				rxTaskWaiting[waitingIndex] = TaskBase::GetCallerTaskHandle();
				rxBuffersWaiting.SetBit(waitingIndex);
				const bool success = (READBITS(hw, RXF1S, F1FL) != 0) || (TaskBase::Take(timeout), READBITS(hw, RXF1S, F1FL) != 0);
				rxBuffersWaiting.ClearBit(waitingIndex);
				if (!success)
				{
					return false;
				}
			}
#else
			while (READBITS(hw, RXF1S, F1FL) == 0)
			{
				if (millis() - start >= timeout)
				{
					return false;
				}
			}
#endif
			// Process the received message into the buffer
			const uint32_t getIndex = (hw->REG(RXF1S) & CAN_(RXF1S_F1GI_Msk)) >> CAN_(RXF1S_F1GI_Pos);
			CopyReceivedMessage(buffer, GetRxFifo1Buffer(getIndex));

			// Tell the hardware that we have taken the message
			WRITEBITS(hw, RXF1A, F1AI, getIndex);
		}
		return true;

	default:
		if ((uint32_t)whichBuffer < (uint32_t)RxBufferNumber::buffer0 + config->numRxBuffers)
		{
			// Check for a received message and wait if necessary
			// We assume that not more than 32 dedicated receive buffers have been configured, so we only need to look at the NDAT1 register
			const uint32_t bufferNumber = (unsigned int)whichBuffer - (unsigned int)RxBufferNumber::buffer0;
			const uint32_t ndatMask = (uint32_t)1 << bufferNumber;
#ifdef RTOS
			if ((hw->REG(NDAT1) & (1ul << (unsigned int)whichBuffer)) == 0)
			{
				if (timeout == 0)
				{
					return false;
				}
				TaskBase::ClearNotifyCount();
				const unsigned int waitingIndex = (unsigned int)whichBuffer;
				rxTaskWaiting[waitingIndex] = TaskBase::GetCallerTaskHandle();
				rxBuffersWaiting.SetBit(waitingIndex);
				const bool success = ((hw->REG(NDAT1) & (1ul << (unsigned int)whichBuffer))) || (TaskBase::Take(timeout), (hw->REG(NDAT1) & (1ul << (unsigned int)whichBuffer)) != 0);
				rxBuffersWaiting.ClearBit(waitingIndex);
				if (!success)
				{
					return false;
				}
			}
#else
			while ((hw->NDAT1.reg & (1ul << (unsigned int)whichBuffer)) == 0)
			{
				if (millis() - start >= timeout)
				{
					return false;
				}
			}
#endif
			// Process the received message into the buffer
			CopyReceivedMessage(buffer, GetRxBuffer(bufferNumber));

			// Tell the hardware that we have taken the message
			hw->REG(NDAT1) = ndatMask;
			return true;
		}
		return false;
	}
}

bool CanDevice::IsMessageAvailable(RxBufferNumber whichBuffer) noexcept
{
	switch (whichBuffer)
	{
	case RxBufferNumber::fifo0:
		return READBITS(hw, RXF0S, F0FL) != 0;
	case RxBufferNumber::fifo1:
		return READBITS(hw, RXF1S, F1FL) != 0;
	default:
		// We assume that not more than 32 dedicated receive buffers have been configured, so we only need to look at the NDAT1 register
		return (hw->REG(NDAT1) & ((uint32_t)1 << ((uint32_t)whichBuffer - (uint32_t)RxBufferNumber::buffer0))) != 0;
	}
}

// Set a short ID field filter element. To disable the filter element, use a zero mask parameter.
// If whichBuffer is a buffer number not a fifo number, the mask field is ignored except that a zero mask disables the filter element; so only the XIDAM mask filters the ID.
void CanDevice::SetShortFilterElement(unsigned int index, RxBufferNumber whichBuffer, uint32_t id, uint32_t mask) noexcept
{
	CanStandardMessageFilterElement::S0Type s0;
	s0.val = 0;										// disable filter, clear reserved fields
	if (mask != 0)
	{
		s0.bit.SFID1 = id;
		s0.bit.SFT = 0x02;							// classic filter
		switch (whichBuffer)
		{
		case RxBufferNumber::fifo0:
			s0.bit.SFEC = 0x01;						// store in FIFO 0
			s0.bit.SFID2 = mask;
			break;
		case RxBufferNumber::fifo1:
			s0.bit.SFEC = 0x02;						// store in FIFO 1
			s0.bit.SFID2 = mask;
			break;
		default:
			s0.bit.SFEC = 0x07;						// store in buffer
			s0.bit.SFID2 = (uint32_t)whichBuffer - (uint32_t)RxBufferNumber::buffer0;
			break;
		}
	}
	rxStdFilter[index].S0.val = s0.val;
}

// Set an extended ID field filter element. To disable the filter element, use a zero mask parameter.
// If whichBuffer is a buffer number not a fifo number, the mask field is ignored except that a zero mask disables the filter element; so only the XIDAM mask filters the ID.
void CanDevice::SetExtendedFilterElement(unsigned int index, RxBufferNumber whichBuffer, uint32_t id, uint32_t mask) noexcept
{
	volatile CanExtendedMessageFilterElement& efp = rxExtFilter[index];
	efp.F0.val = 0;									// disable filter
	if (mask != 0)
	{
		CanExtendedMessageFilterElement::F0Type f0;
		CanExtendedMessageFilterElement::F1Type f1;
		f0.val = 0;									// clear all fields
		f1.val = 0;									// clear all fields
		f1.bit.EFT  = 0x02;							// classic filter
		f0.bit.EFID1 = id;
		switch (whichBuffer)
		{
		case RxBufferNumber::fifo0:
			f0.bit.EFEC = 0x01;						// store in FIFO 0
			f1.bit.EFID2 = mask;
			break;
		case RxBufferNumber::fifo1:
			f0.bit.EFEC = 0x02;						// store in FIFO 1
			f1.bit.EFID2 = mask;
			break;
		default:
			f0.bit.EFEC = 0x07;
			f1.bit.EFID2 = (uint32_t)whichBuffer - (uint32_t)RxBufferNumber::buffer0;
			break;
		}

		efp.F1.val = f1.val;						// update second word first while the filter is disabled
		efp.F0.val = f0.val;						// update first word and enable filter
	}
}

void CanDevice::GetLocalCanTiming(CanTiming &timing) noexcept
{
	const uint32_t localNbtp = hw->REG(NBTP);
	const uint32_t tseg1 = (localNbtp & CAN_(NBTP_NTSEG1_Msk)) >> CAN_(NBTP_NTSEG1_Pos);
	const uint32_t tseg2 = (localNbtp & CAN_(NBTP_NTSEG2_Msk)) >> CAN_(NBTP_NTSEG2_Pos);
	const uint32_t jw = (localNbtp & CAN_(NBTP_NSJW_Msk)) >> CAN_(NBTP_NSJW_Pos);
	const uint32_t brp = (localNbtp & CAN_(NBTP_NBRP_Msk)) >> CAN_(NBTP_NBRP_Pos);
	timing.period = (tseg1 + tseg2 + 3) * (brp + 1);
	timing.tseg1 = (tseg1 + 1) * (brp + 1);
	timing.jumpWidth = (jw + 1) * (brp + 1);
}

void CanDevice::SetLocalCanTiming(const CanTiming &timing) noexcept
{
	UpdateLocalCanTiming(timing);				// set up nbtp and dbtp variables
	Disable();
	hw->REG(NBTP) = nbtp;
	hw->REG(DBTP) = dbtp;
	Enable();
}

void CanDevice::UpdateLocalCanTiming(const CanTiming &timing) noexcept
{
	// Sort out the bit timing
	uint32_t period = timing.period;
	uint32_t tseg1 = timing.tseg1;
	uint32_t jumpWidth = timing.jumpWidth;
	uint32_t prescaler = 1;						// 48MHz main clock
	uint32_t tseg2;

	for (;;)
	{
		tseg2 = period - tseg1 - 1;
		if (tseg1 <= 32 && tseg2 <= 16 && jumpWidth <= 16)
		{
			break;
		}
		prescaler <<= 1;
		period >>= 1;
		tseg1 >>= 1;
		jumpWidth >>= 1;
	}

	nbtp = ((tseg1 - 1) << CAN_(NBTP_NTSEG1_Pos))
		| ((tseg2 - 1) << CAN_(NBTP_NTSEG2_Pos))
		| ((jumpWidth - 1) << CAN_(NBTP_NSJW_Pos))
		| ((prescaler - 1) << CAN_(NBTP_NBRP_Pos));

	// The fast data rate defaults to the same timing
	dbtp = ((tseg1 - 1) << CAN_(DBTP_DTSEG1_Pos))
		| ((tseg2 - 1) << CAN_(DBTP_DTSEG2_Pos))
		| ((jumpWidth - 1) << CAN_(DBTP_DSJW_Pos))
		| ((prescaler - 1) << CAN_(DBTP_DBRP_Pos));
}

void CanDevice::GetAndClearStats(unsigned int& rMessagesQueuedForSending, unsigned int& rMessagesReceived, unsigned int& rTxTimeouts, unsigned int& rMessagesLost, unsigned int& rBusOffCount) noexcept
{
	AtomicCriticalSectionLocker lock;

	rMessagesQueuedForSending = messagesQueuedForSending;
	rMessagesReceived = messagesReceived;
	rMessagesLost = messagesLost;
	rBusOffCount = busOffCount;
	rTxTimeouts = txTimeouts;
	messagesQueuedForSending = messagesReceived = messagesLost = busOffCount = txTimeouts = 0;
}

#ifdef RTOS

void CanDevice::Interrupt() noexcept
{
	uint32_t ir;
	while (((ir = hw->REG(IR)) & (CAN_(IR_RF0N) | CAN_(IR_RF1N) | CAN_(IR_DRX) | CAN_(IR_TC) | CAN_(IR_BO) | CAN_(IR_RF0L) | CAN_(IR_RF1L))) != 0)
	{
		hw->REG(IR) = ir;

		if (ir & CAN_(IR_RF0N))
		{
			constexpr unsigned int waitingIndex = (unsigned int)RxBufferNumber::fifo0;
			TaskBase::GiveFromISR(rxTaskWaiting[waitingIndex]);
			rxBuffersWaiting.ClearBit(waitingIndex);
		}

		if (ir & CAN_(IR_RF1N))
		{
			constexpr unsigned int waitingIndex = (unsigned int)RxBufferNumber::fifo1;
			TaskBase::GiveFromISR(rxTaskWaiting[waitingIndex]);
			rxBuffersWaiting.ClearBit(waitingIndex);
		}

		if (ir & CAN_(IR_DRX))
		{
			// Check which receive buffers have new messages
			if (config->numRxBuffers != 0)		// needed to avoid a compiler warning
			{
				uint32_t newData;
				while ((newData = hw->REG(NDAT1) & (rxBuffersWaiting.GetRaw() >> 2)) != 0)		// bottom 2 bits of rxBuffersWaiting are for the FIFOs
				{
					const unsigned int waitingIndex = LowestSetBit(newData) + (unsigned int)RxBufferNumber::buffer0;
					if (waitingIndex < ARRAY_SIZE(rxTaskWaiting))
					{
						TaskBase::GiveFromISR(rxTaskWaiting[waitingIndex]);
					}
					rxBuffersWaiting.ClearBit(waitingIndex);
				}
			}
		}

		if (ir & CAN_(IR_TC))
		{
			// Check which transmit buffers have finished transmitting
			uint32_t transmitDone;
			while ((transmitDone = (~hw->REG(TXBRP)) & hw->REG(TXBTIE)) != 0)
			{
				const unsigned int bufferNumber = LowestSetBit(transmitDone);
				hw->REG(TXBTIE) &= ~((uint32_t)1 << bufferNumber);
				const unsigned int waitingIndex = bufferNumber + (unsigned int)TxBufferNumber::buffer0;
				if (waitingIndex < ARRAY_SIZE(txTaskWaiting))
				{
					TaskBase::GiveFromISR(txTaskWaiting[waitingIndex]);
				}
				txBuffersWaiting.ClearBit(waitingIndex);
			}

			// Check the tx FIFO
			if ((txBuffersWaiting.GetRaw() & 1u) != 0 && READBITS(hw, TXFQS, TFFL) != 0)
			{
				constexpr unsigned int waitingIndex = (unsigned int)TxBufferNumber::fifo;
				TaskBase::GiveFromISR(txTaskWaiting[waitingIndex]);
				txBuffersWaiting.ClearBit(waitingIndex);
			}
		}

		if (ir & CAN_(IR_BO))
		{
			Disable();
			++busOffCount;
			DoHardwareInit();
			Enable();
		}

		if (ir & (CAN_(IR_RF0L) | CAN_(IR_RF1L)))
		{
			++messagesLost;
		}
	}
}

// Interrupt handlers

void CAN0_Handler() noexcept
{
	devicesByPort[0]->Interrupt();
}

void CAN1_Handler() noexcept
{
	devicesByPort[1]->Interrupt();
}

#endif

#endif
