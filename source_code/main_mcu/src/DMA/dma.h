/*
 * dma.h
 *
 * Created: 29/05/2017 09:18:00
 *  Author: stephan
 */ 


#ifndef DMA_H_
#define DMA_H_

#include "platform_defines.h"
#include "defines.h"

/* Prototypes */
void dma_oled_init_transfer(void* spi_data_p, void* datap, uint16_t size, uint16_t dma_trigger);
uint32_t dma_bootloader_compute_crc32_from_spi(void* spi_data_p, uint32_t size);
void dma_custom_fs_init_transfer(void* spi_data_p, void* datap, uint16_t size);
BOOL dma_custom_fs_check_and_clear_dma_transfer_flag(void);
BOOL dma_oled_check_and_clear_dma_transfer_flag(void);
void dma_init(void);


#endif /* DMA_H_ */