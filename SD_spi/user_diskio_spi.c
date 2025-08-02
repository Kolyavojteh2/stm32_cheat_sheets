/*
 ******************************************************************************
  * @file    user_diskio_spi.c
  * @brief   This file contains the common defines and functions prototypes for
  *          the user_diskio_spi driver implementation
  ******************************************************************************
  * Portions copyright (C) 2014, ChaN, all rights reserved.
  * Portions copyright (C) 2017, kiwih, all rights reserved.
  *
  * This software is a free software and there is NO WARRANTY.
  * No restriction on use. You can use, modify and redistribute it for
  * personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
  * Redistributions of source code must retain the above copyright notice.
  *
  ******************************************************************************
 */

#include "stm32f4xx_hal.h"
#include "user_diskio_spi.h"

/* ---------------------------- SPI Settings --------------------------------- */
#define SD_INI_SPEED	7 	/* Initialization SPI speed:
									0 - FCLK/2,
									1 - FCLK/4,
									2 - FCLK/8,
									3 - FCLK/16,
									4 - FCLK/32,
									5 - FCLK/64,
									6 - FCLK/128,
									7 - FCLK/256    */
#define SD_MAX_SPEED	3 	/* Normal SPI speed */

#define SD_USE_DMA			/* comment out if do not use DMA */

#define GPIO_SD_CS			SD_CS_GPIO_Port
#define GPIO_SD_CS_PIN		SD_CS_Pin

extern SPI_HandleTypeDef SD_SPI_HANDLE;

/* Definitions for MMC/SDC command */
#define CMD0		(0x40+0)	/* GO_IDLE_STATE */
#define CMD1		(0x40+1)	/* SEND_OP_COND (MMC) */
#define ACMD41		(0xC0+41)	/* SEND_OP_COND (SDC) */
#define CMD8		(0x40+8)	/* SEND_IF_COND */
#define CMD9		(0x40+9)	/* SEND_CSD */
#define CMD10		(0x40+10)	/* SEND_CID */
#define CMD12		(0x40+12)	/* STOP_TRANSMISSION */
#define ACMD13		(0xC0+13)	/* SD_STATUS (SDC) */
#define CMD16		(0x40+16)	/* SET_BLOCKLEN */
#define CMD17		(0x40+17)	/* READ_SINGLE_BLOCK */
#define CMD18		(0x40+18)	/* READ_MULTIPLE_BLOCK */
#define CMD23		(0x40+23)	/* SET_BLOCK_COUNT (MMC) */
#define ACMD23		(0xC0+23)	/* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24		(0x40+24)	/* WRITE_BLOCK */
#define CMD25		(0x40+25)	/* WRITE_MULTIPLE_BLOCK */
#define CMD55		(0x40+55)	/* APP_CMD */
#define CMD58		(0x40+58)	/* READ_OCR */

/* MMC card type flags (MMC_GET_TYPE) */
#define CT_MMC		0x01            /* MMC ver 3 */
#define CT_SD1		0x02            /* SD ver 1 */
#define CT_SD2		0x04            /* SD ver 2 */
#define CT_SDC		(CT_SD1|CT_SD2) /* SD */
#define CT_BLOCK	0x08            /* Block addressing */

#define SPI_SD				SD_SPI_HANDLE.Instance
#define SELECT()			(GPIO_SD_CS->BSRR = (uint32_t)GPIO_SD_CS_PIN << 16)
#define DESELECT()			(GPIO_SD_CS->BSRR = GPIO_SD_CS_PIN)

// speed from 0 (FCLK/2) to 7 (FCLK/256)
static void set_sd_interface_speed(uint8_t speed)
{
	if (speed > 7) speed = 7;
	SPI_SD->CR1 &= ~SPI_CR1_SPE;            // Disable SPI
	SPI_SD->CR1 &= ~(0x07UL<<(3U));         // speed mask
	SPI_SD->CR1 |= (uint32_t)(speed<<(3U));
	SPI_SD->CR1 |= SPI_CR1_SPE;             // Enable SPI
}

static BYTE spi_rw(BYTE wval)
{
	*((volatile uint8_t *)&SPI_SD->DR) = wval;
	while(!(SPI_SD->SR & SPI_SR_RXNE)) ;
	while(SPI_SD->SR & SPI_SR_BSY) ;
	return *((volatile uint8_t *)&SPI_SD->DR);
}

#define xmit_spi(dat)		 spi_rw(dat)
#define rcvr_spi()			 spi_rw(0xFF)
#define rcvr_spi_m(dst)  *(dst)=spi_rw(0xFF)

#ifdef SD_USE_DMA

// SPI finished receiving/sending callback
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
	if (hspi == &SD_SPI_HANDLE)
	{
		HAL_SPI_DMAStop(&SD_SPI_HANDLE);
	}
}

typedef enum {
	SPI_Transfer_Receive = 0,
	SPI_Transfer_Transmit
} SPI_TransferType;

// SPI DMA send/receive full duplex master
static void SPI_DMA_TransmitReceive(SPI_TransferType type, const BYTE* buff, uint16_t buff_size)
{
	uint8_t tmp = 0xFF;
	uint8_t *TXdata, *RXdata;
	DMA_Stream_TypeDef *hdma_rx = SD_SPI_HANDLE.hdmarx->Instance;
	DMA_Stream_TypeDef *hdma_tx = SD_SPI_HANDLE.hdmatx->Instance;
	hdma_tx->CR &= ~DMA_SxCR_EN; // disable DMA sending channel
	hdma_rx->CR &= ~DMA_SxCR_EN; // disable DMA receiving channel

	// Wait DMA channels disabling
	while ((hdma_tx->CR & DMA_SxCR_EN) || (hdma_rx->CR & DMA_SxCR_EN)) {__NOP();}
	if(type == SPI_Transfer_Receive)
	{
		hdma_rx->CR &= ~DMA_SxCR_PINC; 	//DMA_PINC_DISABLE
		hdma_rx->CR |= DMA_SxCR_MINC;  	//DMA_MINC_ENABLE
		hdma_tx->CR &= ~(DMA_SxCR_PINC | DMA_SxCR_MINC); 	//DMA_PINC_DISABLE & DMA_MINC_DISABLE
	    TXdata = (uint8_t*)&tmp;
	    RXdata = (uint8_t*)buff;
	}
	else if (type == SPI_Transfer_Transmit)
	{
		hdma_rx->CR &= ~(DMA_SxCR_PINC | DMA_SxCR_MINC); 	//DMA_PINC_DISABLE & DMA_MINC_DISABLE
		hdma_tx->CR &= ~DMA_SxCR_PINC; 	//DMA_PINC_DISABLE
		hdma_tx->CR |= DMA_SxCR_MINC; 	//DMA_MINC_ENABLE
	    TXdata = (uint8_t*)buff;
	    RXdata = (uint8_t*)&tmp;
	}
	HAL_SPI_TransmitReceive_DMA(&SD_SPI_HANDLE, TXdata, RXdata, buff_size);
	while (SD_SPI_HANDLE.State != HAL_SPI_STATE_READY) ;
}

#else
static void spi_r_multi(BYTE *rval, uint16_t cnt)
{
	uint8_t* pRxData = (uint8_t*)rval;
	uint16_t txCnt = cnt;

	while(txCnt > 0)
	{
		*((volatile uint8_t *)&SPI_SD->DR) = 0xFF;
		txCnt--;
		while(!(SPI_SD->SR & SPI_SR_RXNE));
		*pRxData = *((volatile uint8_t *)&SPI_SD->DR);
		pRxData++;
	}
	while(SPI_SD->SR & SPI_SR_BSY) ;
}

#endif // SD_USE_DMA

/* SD card definitions */
static volatile DSTATUS Stat = STA_NOINIT; //	Disk status
static BYTE CardType;	 // Card type flags

static BYTE wait_ready(void)
{
	BYTE res;
	rcvr_spi();
	do {
		res = rcvr_spi();
	} while (res != 0xFF); // check timeout
	return res;
}

static BYTE send_cmd(BYTE cmd, DWORD arg)
{
	BYTE res;

	if(cmd & 0x80){	// ACMD<n> is the command sequence of CMD55-CMD<n>
		cmd &= 0x7F;
		res = send_cmd(CMD55, 0);
		if(res > 1) return res;
	}

	DESELECT();
	SELECT();
	if(wait_ready() != 0xFF){
		return 0xFF;
	}

	// transmit cmd + arg + crc
	xmit_spi(cmd);								// Start + Command index
	xmit_spi((BYTE)(arg >> 24));	// Argument[31..24]
	xmit_spi((BYTE)(arg >> 16));	// Argument[23..16]
	xmit_spi((BYTE)(arg >> 8));		// Argument[15..8]
	xmit_spi((BYTE)arg);					// Argument[7..0]
	BYTE crc = 0x01; // Dummy CRC + Stop
	if(cmd == CMD0) crc = 0x95;			// Valid CRC for CMD0(0)
	if(cmd == CMD8) crc = 0x87;			// Valid CRC for CMD8(0x1AA)
	xmit_spi(crc);

	// Receive command response
	if(cmd == CMD12) rcvr_spi();		// Skip a stuff byte when stop reading

	// Wait for a valid response in timeout of 10 attempts
	BYTE attempt = 10;
	do {
		res = rcvr_spi();
	} while ((res & 0x80) && (--attempt));

	return res;
}

DSTATUS USER_SPI_initialize(BYTE drv)
{
	BYTE n, cmd, ty = 0, ocr[4];
	DESELECT();
	set_sd_interface_speed(SD_INI_SPEED); // Set initialization speed
	SELECT();

	for(n=10; n; n--) rcvr_spi();	// 80 dummy clocks

	if(0x01 == send_cmd(CMD0, 0))
	{
		// Enter Idle state
		if(0x01 == send_cmd(CMD8, 0x1AA))
		{
			// SDHC ?
			for(n=0; n<4; n++) ocr[n] = rcvr_spi(); // Get trailing return value of R7 response

			if((0x01 == ocr[2]) && (0xAA == ocr[3]))
			{
				// The card can work at VDD range of 2.7-3.6V
				while(send_cmd(ACMD41, (1UL << 30)));	// Wait for leaving idle state (ACMD41 with HCS bit)

				if(0x00 == send_cmd(CMD58, 0))
				{
					// Check CCS bit in the OCR
					for(n=0; n<4; n++) ocr[n] = rcvr_spi();
					ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;
				}
			}
		}
		else
		{
			// SDSC or MMC
			if(send_cmd(ACMD41, 0) <= 0x01)
			{
				// SDSC
				ty = CT_SD1;
				cmd = ACMD41;
			}
			else
			{
				// MMC
				ty = CT_MMC;
				cmd = CMD1;
			}

			while(send_cmd(cmd, 0)); // Wait for leaving idle state

			if(send_cmd(CMD16, 512) != 0)	// Set R/W block length to 512
				ty = 0;
		}
	}
	CardType = ty;
	DESELECT();

	if(ty)
	{
		// Initialization succeeded
		Stat &= ~STA_NOINIT; // Clear STA_NOINIT
		set_sd_interface_speed(SD_MAX_SPEED); // set normal SPI speed
	}
	else
	{
		// Initialization failed
		if(!(Stat & STA_NOINIT))
		{
			SELECT();
			wait_ready();
			DESELECT();
		}

		Stat |= STA_NOINIT;
	}

	return Stat;
}

DSTATUS USER_SPI_status(BYTE drv)
{
	return Stat;
}

// NOTE: btr must be multiple of 4
static uint8_t rcvr_datablock(BYTE *buff, UINT btr)
{
	BYTE token;

	do {
		token = rcvr_spi();
	} while (token == 0xFF); // wait for data packet in timeout of 100ms
	if(token != 0xFE) return 0;	// not a valid data token

#ifdef SD_USE_DMA
	SPI_DMA_TransmitReceive(SPI_Transfer_Receive, buff, btr);
#else
	if(btr == 512)
	{
		spi_r_multi(buff, btr);
	}
	else if(btr == 16)
	{
		spi_r_multi(buff, btr);
	}
	else
	{
		do {
			rcvr_spi_m(buff++);
			rcvr_spi_m(buff++);
			rcvr_spi_m(buff++);
			rcvr_spi_m(buff++);
		} while (btr -= 4);
	}
#endif

	rcvr_spi(); // discard CRC
	rcvr_spi();
	return 1;
}

DRESULT USER_SPI_read(BYTE drv, BYTE *buff, DWORD sector, UINT count)
{
	if(!count) return RES_PARERR;
	if(Stat & STA_NOINIT) return RES_NOTRDY;
	if(!(CardType & CT_BLOCK)) sector *= 512;	// Convert to byte address if needed

	if(1 == count)
	{
		// CMD17 - READ_SINGLE_BLOCK
		if(0x00 == send_cmd(CMD17, sector))
		{
			if(rcvr_datablock(buff, 512)) count = 0;
		}
	}
	else
	{
		// CMD18 - READ_MULTIPLE_BLOCK
		if(0x00 == send_cmd(CMD18, sector))
		{
			do {
				if(!rcvr_datablock(buff, 512)) break;
				buff += 512;
			} while (--count);
			send_cmd(CMD12, 0); // CMD12 - STOP_TRANSMISSION
		}
	}
	DESELECT();

	return count ? RES_ERROR : RES_OK;
}

#if _FS_READONLY == 0
// token - Data/Stop token
static uint8_t xmit_datablock(const BYTE *buff, BYTE token)
{
	BYTE resp;
#ifndef SD_USE_DMA
	BYTE wc;
#endif

	if(0xFF != wait_ready()) return 0;
	xmit_spi(token); // transmit Data token

	if(0xFD != token) // Is data token
	{
		// transmit the 512 byte data block to MMC
#ifdef SD_USE_DMA
		SPI_DMA_TransmitReceive(SPI_Transfer_Transmit, buff, 512);
#else
		wc = 0;
		do {
			xmit_spi(*buff++);
			xmit_spi(*buff++);
		} while (--wc);
#endif

		// CRC (Dummy)
		xmit_spi(0xFF);
		xmit_spi(0xFF);

		// Receive data response
		resp = rcvr_spi();
		if((resp & 0x1F) != 0x05) // If not accepted, return with error
			return 0;
	}

	return 1;
}

DRESULT USER_SPI_write(BYTE drv, const BYTE *buff, DWORD sector, UINT count)
{
	if(!count) return RES_PARERR;
	if(Stat & STA_NOINIT) return RES_NOTRDY;
	if(Stat & STA_PROTECT) return RES_WRPRT;
	if(!(CardType & CT_BLOCK)) sector *= 512;	// Convert to byte address if needed

	if(1 == count)
	{
		// CMD24 - WRITE_BLOCK
		if((0x00 == send_cmd(CMD24, sector)) && xmit_datablock(buff, 0xFE)) count = 0;
	}
	else
	{
		// CMD25 - WRITE_MULTIPLE_BLOCK
		if(CardType & CT_SDC) send_cmd(ACMD23, count);
		if(0x00 == send_cmd(CMD25, sector))
		{
			do {
				if(!xmit_datablock(buff, 0xFC)) break;
				buff += 512;
			} while (--count);

			if(!xmit_datablock(0, 0xFD)) // STOP_TRAN token
				count = 1;
		}
	}
	DESELECT();

	return count ? RES_ERROR : RES_OK;
}
#endif /* _READONLY == 0 */

DRESULT USER_SPI_ioctl(BYTE drv, BYTE ctrl,	void *buff)
{
	DRESULT res = RES_ERROR;
	BYTE n, csd[16], *ptr = buff;
	WORD csize;

	if(Stat & STA_NOINIT) return RES_NOTRDY;

	switch(ctrl)
	{
		case CTRL_SYNC:
			// Make sure that no pending write process
			if(0xFF == wait_ready()) res = RES_OK;
		break;

		case GET_SECTOR_COUNT:
			// Get number of sectors on the disk (DWORD)
			if((0x00 == send_cmd(CMD9, 0)) && rcvr_datablock(csd, 16))
			{
				if((csd[0] >> 6) == 1)
				{
					// SDC version 2.00
					csize = csd[9] + ((WORD)csd[8] << 8) + 1;
					*(DWORD*)buff = (DWORD)csize << 10;
				}
				else
				{
					// SDC version 1.XX or MMC
					n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
					csize = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
					*(DWORD*)buff = (DWORD)csize << (n - 9);
				}
				res = RES_OK;
			}
		break;

		case GET_SECTOR_SIZE:
			// Get R/W sector size (WORD)
			*(WORD*)buff = 512;
			res = RES_OK;
		break;

		case GET_BLOCK_SIZE:
			// Get erase block size in unit of sector (DWORD)
			if(CardType & CT_SD2)
			{
				// SDC version 2.00
				if(0x00 == send_cmd(ACMD13, 0))
				{
					// Read SD status
					rcvr_spi();
					if(rcvr_datablock(csd, 16))
					{
						// Read partial block
						for (n = 64 - 16; n; n--) rcvr_spi();	// Purge trailing data
						*(DWORD*)buff = 16UL << (csd[10] >> 4);
						res = RES_OK;
					}
				}
			}
			else
			{
				// SDC version 1.XX or MMC
				if((0x00 == send_cmd(CMD9, 0)) && rcvr_datablock(csd, 16))
				{
					// Read CSD
					if(CardType & CT_SD1)
					{
						// SDC version 1.XX
						*(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
					}
					else
					{
						// MMC
						*(DWORD*)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
					}
					res = RES_OK;
				}
			}
		break;

		case MMC_GET_TYPE:
			// Get card type flags (1 byte)
			*ptr = CardType;
			res = RES_OK;
		break;

		case MMC_GET_CSD:
			// Receive CSD (CMD9) as a data block (16 bytes)
			if((0x00 == send_cmd(CMD9, 0)) && rcvr_datablock(ptr, 16)) res = RES_OK;
		break;

		case MMC_GET_CID:
			// Receive CID (CMD10) as a data block (16 bytes)
			if((0x00 == send_cmd(CMD10, 0)) && rcvr_datablock(ptr, 16))	res = RES_OK;
		break;

		case MMC_GET_OCR:
			// Receive OCR (CMD58) as an R3 resp (4 bytes)
			if(0x00 == send_cmd(CMD58, 0))
			{
				for(n=4; n; n--) *ptr++ = rcvr_spi();
				res = RES_OK;
			}
		break;

		case MMC_GET_SDSTAT:
			// Receive SD status (ACMD13) as a data block (64 bytes)
			if(0x00 == send_cmd(ACMD13, 0))
			{
				rcvr_spi();
				if(rcvr_datablock(ptr, 64)) res = RES_OK;
			}
		break;

		default:
			res = RES_PARERR;
	}

	DESELECT();
	return res;
}
