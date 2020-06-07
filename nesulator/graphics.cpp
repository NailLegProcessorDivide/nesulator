#pragma once
#include "graphics.h"
#include "olcPixelGameEngine.h"

bool close = false;

uint8_t b1 [240 * 256];
uint8_t b2 [240 * 256];
uint8_t *activebuffer = b1;//double buffering

void setpixel(int x, int y, uint8_t colour) {
	//printf("draw\n");
	b1[x + y * 256] = colour;
}

class renderer : public olc::PixelGameEngine {
	bool OnUserCreate() {
		return true;
	}
	bool OnUserUpdate(float fElapsedTime) {
		for (int x = 0; x < ScreenWidth(); x++) {
			for (int y = 0; y < ScreenHeight(); y++) {
				int offset = (x + y * 256) % 0x8000;
				uint8_t col = activebuffer[offset];
				//Draw(x, y, olc::Pixel(activebuffer[(x + y * 256) * 3], activebuffer[(x + y * 256) * 3 + 1], activebuffer[(x + y * 256) * 3 + 2]));
				Draw(x, y, olc::Pixel(col, col, col));
			}
		}
		return !close;
	}
	bool OnUserDestroy() {
		return true;
	}
};

void setclose() {
	close = true;
}

void setpointer(uint8_t *p) {
	//activebuffer = p;
}

void start() {
	renderer r = renderer();
	if (r.Construct(256, 240, 4, 4)) {
		r.Start();
	}
}