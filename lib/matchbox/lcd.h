/*
 * LcdInterface.h
 *
 *  Created on: Jan 16, 2012
 *      Author: jmiller
 */

#ifndef LCD_H_
#define LCD_H_

#include <stdint.h>
#include "stm32f4xx_hal_dma.h"
#include "stm32f4xx_hal_spi.h"
#include "OrderedDither.h"
#include "font.h"

// TODO: Move these to a config file
#define LCD_PEN_PIN PB9
#define LCD_SCLK_PIN PB10
#define LCD_SI_PIN PC3
#define LCD_SCS_PIN PB1
#define LCD_EXTC_PIN PB4
#define LCD_DISP_PIN PB5
#define LCD_SIZE (YRES * LCD_LINE_SIZE) // size of lcd frame in bytes
#define LCD_CHAN 3 // 3 == color, 1 == mono
#define LCD_XRES 128
#define LCD_YRES 128
#define LCD_LINE_SIZE (LCD_CHAN*LCD_XRES/8) // size of a line in bytes (data only)

class Lcd {
	public:
        enum FontSize { FONT_SMALL, FONT_MEDIUM, FONT_LARGE };
        Lcd(SPI_HandleTypeDef& spi,
                uint8_t en = LCD_PEN_PIN,
                uint8_t sclk = LCD_SCLK_PIN,
                uint8_t si = LCD_SI_PIN,
                uint8_t scs = LCD_SCS_PIN,
                uint8_t extc = LCD_EXTC_PIN,
                uint8_t disp = LCD_DISP_PIN);
		void begin(void);
		void clear(uint8_t r, uint8_t g, uint8_t b);
		void setPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);
		void circle(int x0, int y0, int radius, uint8_t r, uint8_t g, uint8_t b);
		void line(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b);
		void rect(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, bool fill = true);
		int putChar(uint8_t c, int x, int y);
		void putString(const char *str, int x, int y);

		int rows() const { return _yres; }
		int cols() const { return _xres; }
		void refresh();

	private:
		struct Line {
		        uint8_t cmd;
		        uint8_t row;
		        uint8_t data[LCD_LINE_SIZE];
		};

		void sendByte(uint8_t b);
		void sendBytes(uint8_t* data, uint16_t count);
		void refreshLineSpi();
		void refreshFrameSpi();
		void refreshLine();
        void refreshLine(int row);
		void refreshFrame();

		// SPI bus
		SPI_HandleTypeDef& _spi;

		// IRQ/DMA handler data
		uint8_t _frame; // refresh frame count
		uint8_t _row; // refresh rown count

		// Inlining these methods really speed things up but cost lots of space...
		int dither(int x, int y, int r, int g, int b);
		void color(int color);
		const int _xres, _yres, _channels, _line_size;
		const uint8_t _en, _sclk, _si, _scs, _extc, _disp;
		uint8_t _clear;
		OrderedDither _dither;
		const Font* _currentFont;
		struct Line _frameBuffer[LCD_YRES];
};

#endif /* LCD_H_ */
