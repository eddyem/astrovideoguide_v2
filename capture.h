/*
 * capture.h
 *
 * Copyright 2014 Edward V. Emelianov <eddy@sao.ru, edward.emelianoff@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#pragma once
#ifndef __CAPTURE_H__
#define __CAPTURE_H__

#include <stdint.h> // uint8_t

// max amount of image reading tries
#ifndef MAX_READING_TRIES
	#define MAX_READING_TRIES		10
#endif

extern int videodev_prepared;
int prepare_videodev(char *dev, int channel);
void list_all_inputs(char *dev);
uint8_t *capture_frame();
int capture_frames(int istart, int N);
void free_videodev();

uint8_t *getpng(size_t *size, int w, int h, uint8_t *data);
uint8_t *getjpg(size_t *size, int w, int h, uint8_t *data);

#endif // __CAPTURE_H__
