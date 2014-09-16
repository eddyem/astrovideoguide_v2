/*
 * main.c
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

#include "main.h"
#include "capture.h"

glob_pars *Global_parameters = NULL;

int globErr = 0; // errno for WARN/ERR
// function pointers for coloured output
int (*red)(const char *fmt, ...);
int (*_WARN)(const char *fmt, ...);
int (*green)(const char *fmt, ...);
/*
 * format red / green messages
 * name: r_pr_, g_pr_
 * @param fmt ... - printf-like format
 * @return number of printed symbols
 */
int r_pr_(const char *fmt, ...){
	va_list ar; int i;
	printf(RED);
	va_start(ar, fmt);
	i = vprintf(fmt, ar);
	va_end(ar);
	printf(OLDCOLOR);
	return i;
}
int g_pr_(const char *fmt, ...){
	va_list ar; int i;
	printf(GREEN);
	va_start(ar, fmt);
	i = vprintf(fmt, ar);
	va_end(ar);
	printf(OLDCOLOR);
	return i;
}
/*
 * print red error/warning messages (if output is a tty)
 * @param fmt ... - printf-like format
 * @return number of printed symbols
 */
int r_WARN(const char *fmt, ...){
	va_list ar; int i = 1;
	fprintf(stderr, RED);
	va_start(ar, fmt);
	if(globErr){
		errno = globErr;
		vwarn(fmt, ar);
		errno = 0;
		globErr = 0;
	}else
		i = vfprintf(stderr, fmt, ar);
	va_end(ar);
	i++;
	fprintf(stderr, OLDCOLOR "\n");
	return i;
}
const char stars[] = "****************************************";
/*
 * notty variants of coloured printf
 * name: s_WARN, r_pr_notty
 * @param fmt ... - printf-like format
 * @return number of printed symbols
 */
int s_WARN(const char *fmt, ...){
	va_list ar; int i;
	i = fprintf(stderr, "\n%s\n", stars);
	va_start(ar, fmt);
	if(globErr){
		errno = globErr;
		vwarn(fmt, ar);
		errno = 0;
		globErr = 0;
	}else
	i = +vfprintf(stderr, fmt, ar);
	va_end(ar);
	i += fprintf(stderr, "\n%s\n", stars);
	i += fprintf(stderr, "\n");
	return i;
}
int r_pr_notty(const char *fmt, ...){
	va_list ar; int i;
	i = printf("\n%s\n", stars);
	va_start(ar, fmt);
	i += vprintf(fmt, ar);
	va_end(ar);
	i += printf("\n%s\n", stars);
	return i;
}


int main(int argc, char **argv){
	// setup coloured output
	if(isatty(STDOUT_FILENO)){ // make color output in tty
		red = r_pr_; green = g_pr_;
    }else{ // no colors in case of pipe
		red = r_pr_notty; green = printf;
    }
	if(isatty(STDERR_FILENO)) _WARN = r_WARN;
		else _WARN = s_WARN;
	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");
	bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
	/*
	 * To run in GUI bullshit (like qt or gtk) you must do:
	 * bind_textdomain_codeset(PACKAGE, "UTF8");
	 */
	textdomain(GETTEXT_PACKAGE);
	Global_parameters = parce_args(argc, argv);
	assert(Global_parameters != NULL);
	DBG("videodev: %s, channel: %d\n", Global_parameters->videodev, Global_parameters->videochannel);
	DBG("list: %d\n", Global_parameters->listchannels);
	if(Global_parameters->listchannels)
		list_all_inputs(Global_parameters->videodev);
	if(!prepare_videodev(Global_parameters->videodev, Global_parameters->videochannel)){
		/// "Не могу подготовить видеоустройство к работе"
		ERR(_("Can't prepare video device"));
	}
	capture_frame(1,3);
	free_videodev();
	sleep(2);
	prepare_videodev(Global_parameters->videodev, Global_parameters->videochannel);
	sleep(2);
	capture_frame(4,3);
	free_videodev();
	return 0;
}
