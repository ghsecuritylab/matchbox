/*
 * Lcd.cpp
 *
 *  Created on: Jan 16, 2012
 *      Author: jmiller
 */

#include <algorithm>
#include <cstring>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include "font.h"
#include "gpio.h"
#include "lcd.h"
#include "util.h"

#define BLE_PRESENT

Lcd::Lcd(Spi& spi, const Config& config) : _spi(spi), _config(config),
        _clear(1), _frame(0), _currentFont(getFont("roboto_bold_10")),
        _xres(LCD_XRES), _yres(LCD_YRES), _channels(LCD_CHAN), _line_size(LCD_XRES*LCD_CHAN/8),
        _refreshBuffer(new Frame()), _doSwap(true)
{
    bool doubleBuffer = config.doubleBuffer;
    _writeBuffer = doubleBuffer ? new Frame() : _refreshBuffer;
    memset(_fg, 0, sizeof(_fg));
    memset(_bg, 0xff, sizeof(_bg));
}

Lcd::~Lcd() {
    if (_writeBuffer != _refreshBuffer) {
        delete _writeBuffer; // double buffered
    }
    delete _refreshBuffer;
}

void Lcd::begin() {
    // Skip sclk and si since SPI initializes them for alternate functionality (spi)
#ifdef BLE_PRESENT // BLE uses extc for MISO. Use software refresh
    uint8_t pins[] = {_config.en, _config.scs };
    uint8_t defaults[] = { 1, 0 };
#else
    uint8_t pins[] = {_config.en, _config.scs, _config.extc, _config.disp };
    uint8_t defaults[] = { 1, 0, 0, 1 };
#endif
    for (int i = 0; i < Number(pins); i++) {
        pinInitOutput(pins[i], defaults[i]);
    }
    bzero(_refreshBuffer, sizeof(*_refreshBuffer));
    refreshFrame(); // start SPI transfer chain
}

void Lcd::setEnabled(bool enabled) {
    bool en = readPin(_config.en);
    if (enabled != en) {
        if (enabled) {
            // restart refresh
            // refreshFrame();
        }
        writePin(_config.en, enabled ? 1 : 0);
    }
}

// Called when SPI finishes transfering the current frame
void Lcd::refreshFrameCallback(void* arg) {
    Lcd* thisPtr = (Lcd*) arg;
    thisPtr->refreshFrame(); // frame done, do it again! TODO: Maybe limit this to 30Hz
}

// Single line format : SCS CMD ROW <data0..n> 0 0 SCS#
// Multi-line format : SCS CMD ROW <data0..n> IGNORED ROW <data0..127> ... SCS#
void Lcd::refreshFrame() {
    if (_doSwap) {
        std::swap(_refreshBuffer, _writeBuffer);
        _doSwap = false;
    }
#ifndef BLE_PRESENT
    writePin(_config.extc, (_frame) & 0x01); // Toggle common driver once per frame
#else
    // TODO: ensure _clear is always in the correct state
    _refreshBuffer->line[0].cmd = bitSwap(0x80 | (_frame ? 0x40:0) | (_clear ? 0x20 : 0));
#endif

    writePin(_config.scs, 0); // cs disabled
    writePin(_config.scs, 1); // cs enabled
    Spi::Status status = _spi.transmit((uint8_t*)_refreshBuffer, sizeof(*_refreshBuffer),
            refreshFrameCallback, this);
    if (Spi::OK != status) {
        error("Failed to refresh with status=%d\n", status);
    }
    _clear = 0;
    _frame++; // Toggle common driver once per frame
}

void
Lcd::clear(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t p[3];
    // TODO: handle monochrome displays and dithering...
    uint32_t* pixel = (uint32_t*)BITBAND_SRAM((int) &p[0], 0); // addr of first pixel
    for (int i = 0; i < _channels * 8; i+=_channels) {
        *pixel++ = r ? 1 : 0;
        *pixel++ = g ? 1 : 0;
        *pixel++ = b ? 1 : 0;
    }
    for (int j = 0; j < _yres; j++) {
        uint8_t* pixels = &_writeBuffer->line[j].data[0];
        for (int i = 0; i < _line_size/3; i++) {
            *pixels++ = p[0];
            *pixels++ = p[1];
            *pixels++ = p[2];
        }
    }
    // Initialize the cmd/row data. If this gets clobbered, the LCD won't update.
    const uint8_t cmd = bitSwap(0x80 | (_frame ? 0x40:0) | (_clear ? 0x20 : 0));
    for (int i = 0; i < _yres; i++) {
        _writeBuffer->line[i].cmd = cmd;
        _writeBuffer->line[i].row = i + 1;
    }
}

void Lcd::line(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
	int dx = abs(x1-x0);
	int dy = abs(y1-y0);
	const int stepX = x0 < x1 ? 1 : -1;
	const int stepY = y0 < y1 ? 1 : -1;
	int err = dx - dy;
    uint32_t pixaddr = x0 * _channels;
	while (x0 != x1 || y0 != y1) {
		setPixel(x0, y0, r, g, b); // TODO: optimize with bit banding
		int e2 = err << 1;
		if (e2 <  dx) {
		   err += dx;
		   y0 += stepY;
		}
		if (e2 > -dy) {
		   err -= dy;
		   x0 += stepX;
		}
	}
}

void Lcd::circle(int x0, int y0, int radius, uint8_t r, uint8_t g, uint8_t b)
{
	int f = 1 - radius;
	int ddF_x = 0;
	int ddF_y = -2 * radius;
	int x = 0;
	int y = radius;
	setPixel(x0, y0 + radius, r, g, b);
	setPixel(x0, y0 - radius, r, g, b);
	setPixel(x0 + radius, y0, r, g, b);
	setPixel(x0 - radius, y0, r, g, b);
	while (x < y) {
		if (f >= 0) {
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x + 1;
		setPixel(x0 + x, y0 + y, r, g, b);
		setPixel(x0 - x, y0 + y, r, g, b);
		setPixel(x0 + x, y0 - y, r, g, b);
		setPixel(x0 - x, y0 - y, r, g, b);
		setPixel(x0 + y, y0 + x, r, g, b);
		setPixel(x0 - y, y0 + x, r, g, b);
		setPixel(x0 + y, y0 - x, r, g, b);
		setPixel(x0 - y, y0 - x, r, g, b);
	}
}

void Lcd::rect(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, bool fill)
{
	if (fill) {
		const int xmin = std::min(x0, x1);
		const int xmax = std::max(x0, x1);
		const int ymin = std::min(y0, y1);
		const int ymax = std::max(y0, y1);
		for (int j = ymin; j < ymax; j++) {
		    for (int i = xmin; i < xmax; i++) {
		        setPixel(i, j, r, g, b);
		    }
		}
	} else {
		line(x0, y0, x1, y0, r, g, b);
		line(x0, y1, x1, y1, r, g, b);
		line(x0, y0, x0, y1, r, g, g);
		line(x1, y0, x1, y1, r, g, b);
	}
}

int Lcd::putChar(uint8_t c, int x, int y, const uint8_t* fg, const uint8_t* bg) {

    if (!_currentFont) return 0;

    // Find character to print. TODO: use binary search
    const CharData* charData = 0;
    for (int i = 0; i < _currentFont->charCount; i++) {
        if (c == _currentFont->charData[i].ch) {
            charData = &_currentFont->charData[i];
            break;
        }
    }

    if (!charData) {
        return 0; // character not found
    }

	for (int j = 0; j < charData->height; j++) {
		for (int i = 0; i < charData->width; i++) {
		    const int bitAddr = (charData->y + j) * _currentFont->width + (charData->x + i);
		    const int byteAddr = bitAddr / 8;
		    const int bitMask = 1 << (bitAddr % 8);
			const uint8_t* color = (_currentFont->bitmap[byteAddr] & bitMask) ? bg : fg;
			if (color) {
			    setPixel(i + x, j + y, color[0], color[1], color[2]);
			}
		}
	}
	return charData->width;
}

void Lcd::putString(const char *str, int x, int y, const uint8_t* fg, const uint8_t *bg) {
	while (char ch = *str++) {
		x += putChar(ch, x, y, fg, bg);
	}
}
