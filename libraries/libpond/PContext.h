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

#ifndef DUCKOS_LIBPOND_PCONTEXT_H
#define DUCKOS_LIBPOND_PCONTEXT_H

#include <sys/types.h>
#include <sys/socketfs.h>
#include <cstdarg>
#include <map>
#include "PWindow.h"
#include "PEvent.h"
#include "packet.h"

/**
 * The class used for interfacing with the Pond server.
 */
class PContext {
public:
	/**
	 * Initializes the connection to pond.
	 * @return A Pond context if successful, nullptr if not.
	 */
	static PContext* init();

	/**
	 * Returns whether or not there is an event waiting to be read with ::next_event().
	 * @return Whether or not there is an event waiting.
	 */
	bool has_event();

	/**
	 * Waits for the next event from pond and returns it.
	 * @return The event.
	 */
	PEvent next_event();

	/**
	 * Creates a window.
	 * @param parent NULL, or the parent window.
	 * @param x The x position of the window.
	 * @param y The y position of the window.
	 * @param width The width of the window.
	 * @param height The height of the window.
	 * @return A PWindow object or NULL if the creation failed.
	 */
	PWindow* create_window(PWindow* parent, int x, int y, int width, int height);

	/**
	 * Sends a packet to the pond server.
	 * @return Whether or not the write was successful.
	 */
	template<typename T>
	bool send_packet(const T& packet) {
		return write_packet(fd, SOCKETFS_HOST, sizeof(T), (void*) &packet) >= 0;
	}

private:
	explicit PContext(int _fd);

	void handle_open_window(socketfs_packet* packet, PEvent* event);
	void handle_destroy_window(socketfs_packet* packet, PEvent* event);
	void handle_move_window(socketfs_packet* packet, PEvent* event);
	void handle_resize_window(socketfs_packet* packet, PEvent* event);
	void handle_mouse_move(socketfs_packet* packet, PEvent* event);
	void handle_mouse_button(socketfs_packet* packet, PEvent* event);
	void handle_key(socketfs_packet* packet, PEvent* event);

	int fd;
	std::map<int, PWindow*> windows;
};

#endif //DUCKOS_LIBPOND_PCONTEXT_H