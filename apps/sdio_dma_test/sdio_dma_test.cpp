/*
 * sdio_dma_test.cpp
 *
 *  Created on: Jun 20, 2016
 *      Author: jmiller
 */

//#define DEBUG

#include <string.h>
#include <stdlib.h>
#include "stm32f4xx_hal.h"
#include "matchbox.h"
#include "pin.h"
#include "util.h"

enum DeathCode {
    SDIO_INIT = MatchBox::CODE_LAST, // blink codes start here
    SDIO_DMA,
    SDIO_DMA3,
    SDIO_DMA6,
    SDIO_HS_MODE,
    BUFFER_NOT_ALIGNED
};

#define DMA_NVIC_PRIORITY 6
#define SD_NVIC_PRIORITY 5 // must be lower than DMA_NVIC_PRIORITY
#define SD_DMAx_Tx_CHANNEL                DMA_CHANNEL_4
#define SD_DMAx_Rx_CHANNEL                DMA_CHANNEL_4
#define SD_DMAx_TxRx_CLK_ENABLE           __HAL_RCC_DMA2_CLK_ENABLE
#define SD_DMAx_Tx_STREAM                 DMA2_Stream6
#define SD_DMAx_Rx_STREAM                 DMA2_Stream3
#define SD_DMAx_Tx_IRQn                   DMA2_Stream6_IRQn
#define SD_DMAx_Rx_IRQn                   DMA2_Stream3_IRQn

static osThreadId defaultTaskHandle;
static SD_HandleTypeDef uSdHandle;
static HAL_SD_CardInfoTypedef uSdCardInfo;
static Pin* led;

void StartDefaultTask(void const * argument);

int main(void) {
    MatchBox* mb = new MatchBox(MatchBox::C168MHz);

    osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 2048);
    defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

    /* Start scheduler */
    osKernelStart();

    /* We should never get here as control is now taken by the scheduler */
    while (1)
        ;
    return 0;
}

bool sdDmaInit(void) {
    static DMA_HandleTypeDef dmaRxHandle;
    static DMA_HandleTypeDef dmaTxHandle;
    bool result = true;

    /* Configure DMA Rx parameters */
    dmaRxHandle.Instance = SD_DMAx_Rx_STREAM;
    dmaRxHandle.Init.Channel = SD_DMAx_Rx_CHANNEL;
    dmaRxHandle.Init.Direction = DMA_PERIPH_TO_MEMORY;
    dmaRxHandle.Init.PeriphInc = DMA_PINC_DISABLE;
    dmaRxHandle.Init.MemInc = DMA_MINC_ENABLE;
    dmaRxHandle.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    dmaRxHandle.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    dmaRxHandle.Init.Mode = DMA_PFCTRL;
    dmaRxHandle.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    dmaRxHandle.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    dmaRxHandle.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    dmaRxHandle.Init.MemBurst = DMA_MBURST_INC4;
    dmaRxHandle.Init.PeriphBurst = DMA_PBURST_INC4;

    /* Associate the DMA handle */
    __HAL_LINKDMA(&uSdHandle, hdmarx, dmaRxHandle);

    /* Deinitialize the stream for new transfer */
    if (HAL_DMA_DeInit(&dmaRxHandle) != HAL_OK) {
        debug("Failed to DeInit Rx DMA\n");
        result = false;
    }

    /* Configure the DMA stream */
    if (HAL_DMA_Init(&dmaRxHandle) != HAL_OK) {
        debug("Failed to Init Rx DMA\n");
        result = false;
    }

    /* Configure DMA Tx parameters */
    dmaTxHandle.Instance = SD_DMAx_Tx_STREAM;
    dmaTxHandle.Init.Channel = SD_DMAx_Tx_CHANNEL;
    dmaTxHandle.Init.Direction = DMA_MEMORY_TO_PERIPH;
    dmaTxHandle.Init.PeriphInc = DMA_PINC_DISABLE;
    dmaTxHandle.Init.MemInc = DMA_MINC_ENABLE;
    dmaTxHandle.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    dmaTxHandle.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    dmaTxHandle.Init.Mode = DMA_PFCTRL;
    dmaTxHandle.Init.Priority = DMA_PRIORITY_LOW;
    dmaTxHandle.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    dmaTxHandle.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    dmaTxHandle.Init.MemBurst = DMA_MBURST_INC4;
    dmaTxHandle.Init.PeriphBurst = DMA_PBURST_INC4;

    /* Associate the DMA handle */
    __HAL_LINKDMA(&uSdHandle, hdmatx, dmaTxHandle);

    /* Deinitialize the stream for new transfer */
    if (HAL_DMA_DeInit(&dmaTxHandle) != HAL_OK) {
        error("Failed to DeInit Tx DMA\n");
        result = false;
    }

    /* Configure the DMA stream */
    if (HAL_DMA_Init(&dmaTxHandle) != HAL_OK) {
        error("Failed to Init Tx DMA\n");
        result = false;
    }

    /* NVIC configuration for DMA transfer complete interrupt */
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    HAL_NVIC_SetPriority(SD_DMAx_Rx_IRQn, DMA_NVIC_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(SD_DMAx_Rx_IRQn);

    /* NVIC configuration for DMA transfer complete interrupt */
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    HAL_NVIC_SetPriority(SD_DMAx_Tx_IRQn, DMA_NVIC_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(SD_DMAx_Tx_IRQn);

    return result;
}

bool sdInit() {
    /* Enable SDIO clock */
    __HAL_RCC_SDIO_CLK_ENABLE();

    /* Enable DMA2 clocks */
    SD_DMAx_TxRx_CLK_ENABLE();

    /* Enable GPIOs clock */
    __GPIOC_CLK_ENABLE(); // sd data lines PC8..PC12
    __GPIOD_CLK_ENABLE(); // cmd line D2
    __GPIOB_CLK_ENABLE(); // pin detect

    /* Common GPIO configuration */
    GPIO_InitTypeDef GPIO_Init_Structure = { 0 };
    GPIO_Init_Structure.Mode = GPIO_MODE_AF_PP;
    GPIO_Init_Structure.Pull = GPIO_PULLUP;
    GPIO_Init_Structure.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_Init_Structure.Alternate = GPIO_AF12_SDIO;

    /* GPIOC configuration */
    GPIO_Init_Structure.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &GPIO_Init_Structure);

    /* GPIOD configuration */
    GPIO_Init_Structure.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &GPIO_Init_Structure);

    /* SD Card detect pin configuration */
    GPIO_Init_Structure.Mode = GPIO_MODE_INPUT;
    GPIO_Init_Structure.Pull = GPIO_PULLUP;
    GPIO_Init_Structure.Speed = GPIO_SPEED_LOW;
    GPIO_Init_Structure.Pin = 8;
    HAL_GPIO_Init(GPIOB, &GPIO_Init_Structure);

    /* Initialize SD interface
     * Note HW flow control must be disabled on STM32f415 due to hardware glitches on the SDIOCLK
     * line. See errata:
     *
     * "When enabling the HW flow control by setting bit 14 of the SDIO_CLKCR register to ‘1’,
     * glitches can occur on the SDIOCLK output clock resulting in wrong data to be written
     * into the SD/MMC card or into the SDIO device. As a consequence, a CRC error will be
     * reported to the SD/SDIO MMC host interface (DCRCFAIL bit set to ‘1’ in SDIO_STA register)."
     **/
    uSdHandle.Instance = SDIO;
    uSdHandle.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
    uSdHandle.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
    uSdHandle.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
    uSdHandle.Init.BusWide = SDIO_BUS_WIDE_1B;
    uSdHandle.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    uSdHandle.Init.ClockDiv = SDIO_TRANSFER_CLK_DIV;

    HAL_SD_ErrorTypedef status;
    if ((status = HAL_SD_Init(&uSdHandle, &uSdCardInfo)) != SD_OK) {
        error("Failed to init sd: status=%d\n", status);
        return false;
    }

    if ((status = HAL_SD_WideBusOperation_Config(&uSdHandle, SDIO_BUS_WIDE_4B)) != SD_OK) {
        error("Failed to init wide bus mode, status=%d\n", status);
        return false;
    }

    /* NVIC configuration for SDIO interrupts */
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    HAL_NVIC_SetPriority(SDIO_IRQn, SD_NVIC_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);

    // try high speed mode
    if ((status = HAL_SD_HighSpeed(&uSdHandle)) != SD_OK) {
        error("Failed to set high speed mode, status=%d\n", status);
        MatchBox::blinkOfDeath(*led, (MatchBox::BlinkCode) SDIO_HS_MODE);
    }

    return true;
}

int readBlock(void* data, uint64_t addr) {
    HAL_SD_ErrorTypedef status;
    if ((status = HAL_SD_ReadBlocks_DMA(&uSdHandle, (uint32_t*) data, addr, 512, 1)) != SD_OK) {
        error("Failed to read block: status = %d\n", status);
        return 0;
    }
    debug("%s: check\n", __func__);
    if ((status = HAL_SD_CheckReadOperation(&uSdHandle, -1)) != SD_OK) {
        error("HAL_SD_CheckReadOperation() failed, status=%d\n", status);
        return 0;
    }
    debug("%s: done\n", __func__);
    return 1;
}

int writeBlock(void* data, uint64_t addr) {
    HAL_SD_ErrorTypedef status;
    debug("%s\n", __func__);
    if ((status = HAL_SD_WriteBlocks_DMA(&uSdHandle, (uint32_t*) data, addr, 512, 1)) != SD_OK) {
        error("Failed to write block: status = %d\n", status);
        return 0;
    }
    debug("%s: check\n", __func__);
    if ((status = HAL_SD_CheckWriteOperation(&uSdHandle, -1)) != SD_OK) {
        error("HAL_SD_CheckWriteOperation() failed, status=%d\n", status);
        return 0;
    }
    debug("%s: done\n", __func__);
    return 1;
}

void dumpBlock(uint8_t* data, int count) {
    for (int i = 0; i < 512; i++) {
        if ((i % 16) == 0) {
            debug("%08x: ", count * 512 + i);
        }
        debug("%02x", data[i]);
        if ((i % 16) == 15) {
            debug(" ");
            for (int j = 15; j > 0; j--) {
                int ch = data[i - j];
                ch = ch < 32 || ch > 127 ? '.' : ch;
                printf("%c", ch);
            }
            debug((i % 16) == 15 ? "\n" : " ");
        }
    }
}

extern "C" {
    int SDGetStatus(SD_HandleTypeDef *hsd);
    void DMA2_Stream0_IRQHandler();
    void DMA2_Stream1_IRQHandler();
    void DMA2_Stream2_IRQHandler();
    void DMA2_Stream3_IRQHandler();
    void DMA2_Stream4_IRQHandler();
    void DMA2_Stream5_IRQHandler();
    void DMA2_Stream6_IRQHandler();
    void DMA2_Stream7_IRQHandler();
    void SDIO_IRQHandler(void);
    void HAL_SD_XferCpltCallback(SD_HandleTypeDef* hsd);
    void HAL_SD_XferErrorCallback(SD_HandleTypeDef* hsd);
}

void SDIO_IRQHandler(void) {
    int fl = __HAL_SD_SDIO_GET_FLAG(&uSdHandle, SDIO_FLAG_DATAEND);
    debug("%s: end=%d\n", __func__, fl);
    HAL_SD_IRQHandler(&uSdHandle);
}

void DMA2_Stream0_IRQHandler() { debug("%s\n", __func__); }
void DMA2_Stream1_IRQHandler() { debug("%s\n", __func__); }
void DMA2_Stream2_IRQHandler() { debug("%s\n", __func__); }
void DMA2_Stream4_IRQHandler() { debug("%s\n", __func__); }
void DMA2_Stream5_IRQHandler() { debug("%s\n", __func__); }
void DMA2_Stream7_IRQHandler() { debug("%s\n", __func__); }

void HAL_SD_XferCpltCallback(SD_HandleTypeDef* hsd) {
    debug("%s\n", __func__);
}

void HAL_SD_XferErrorCallback(SD_HandleTypeDef* hsd) {
    error("%s\n", __func__);
}

void DMA2_Stream3_IRQHandler() {
    debug("%s\n", __func__);
    HAL_DMA_IRQHandler(uSdHandle.hdmarx);
}

void DMA2_Stream6_IRQHandler() {
    debug("%s, tx=%p\n", __func__, uSdHandle.hdmatx);
    HAL_DMA_IRQHandler(uSdHandle.hdmatx);
}

void StartDefaultTask(void const * argument) {
    Pin _led(LED_PIN, Pin::Config().setMode(Pin::MODE_OUTPUT));
    led = &_led;
    printf("Initializing SDIO DMA test...\n");
    if (!sdInit()) {
        error("Failed to initialize sdio\n");
        MatchBox::blinkOfDeath(_led, (MatchBox::BlinkCode) SDIO_INIT);
    }
    if (!sdDmaInit()) {
        debug("Failed to initialize sdio dma\n");
        MatchBox::blinkOfDeath(_led, (MatchBox::BlinkCode) SDIO_DMA);
    }

    int count = 0;
    uint8_t buff[512];
    if (uint32_t(&buff[0]) & 0x3 != 0) {
        error("Buffer not aligned!\n");
        MatchBox::blinkOfDeath(_led, (MatchBox::BlinkCode) BUFFER_NOT_ALIGNED);
    }
    bzero(buff, sizeof(buff));

    // Seed with random value
    srand(HAL_GetTick());

    printf("Starting DMA\n");
    while (1) {
        char tmp[sizeof(buff)]; // temporary read buffer, for verification
        for (int i = 0; i < sizeof(buff); i++) {
            buff[i] = rand() & 0xff;
        }
        writeBlock(&buff[0], count * 512);
//        while (0x100 != (SDGetStatus(&uSdHandle) & 0x100))
//            ;
//        printf("read..\n");
        readBlock(&tmp[0], count * 512);
        if (0 != memcmp(tmp, buff, sizeof(buff))) {
            debug("*** readback error: blocks differ! ***\n");
        } else {
            if (!(count % 64)) {
                printf("\n");
                printf("Block %08x", count);
            } else {
                printf(".");
            }
        }
        //dumpBlock(&buff[0], count);
        _led.write(count++ & 1);
    }
}
