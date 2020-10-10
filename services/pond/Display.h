/*
    This file is part of duckOS.

    duckOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    duckOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with duckOS.  If not, see <https://www.gnu.org/licenses/>.

    Copyright (c) Byteduck 2016-2020. All rights reserved.
*/

#ifndef DUCKOS_DISPLAY_H
#define DUCKOS_DISPLAY_H

#include <cstdint>

struct Pixel {
	uint8_t b;
	uint8_t g;
	uint8_t r;
	uint8_t : 8;
};

class Display {
public:
	Display();
	int width();
	int height();
	Pixel* framebuffer();
	void clear(Pixel color);

private:
	int _width = -1;
	int _height = -1;
	int framebuffer_fd = 0;
	Pixel* _framebuffer;
};


#endif //DUCKOS_DISPLAY_H
