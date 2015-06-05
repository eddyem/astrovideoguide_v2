/*
 * cmdlnopts.c - the only function that parce cmdln args and returns glob parameters
 *
 * Copyright 2013 Edward V. Emelianoff <eddy@sao.ru>
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
#include "cmdlnopts.h"
#include "main.h"

/*
 * here are global parameters initialisation
 */
glob_pars G;  // internal global parameters structure
int help = 0; // whether to show help string

glob_pars Gdefault = {
	.videodev        = "/dev/video0",
	.videochannel    = 0,
	.listchannels    = FALSE,
	.nodaemon        = FALSE,
	.port            = "54321",
};

/*
 * Define command line options by filling structure:
 *	name	has_arg	flag	val		type		argptr			help
*/
myoption cmdlnopts[] = {
	/// "отобразить это сообщение"
	{"help",	0,	NULL,	'h',	arg_int,	APTR(&help),		N_("show this help")},
	/// "путь к устройству видеозахвата"
	{"videodev",1,	NULL,	'd',	arg_string,	APTR(&G.videodev),	N_("input video device")},
	/// "номер канала захвата"
	{"channel", 1,	NULL,	'n',	arg_int,	APTR(&G.videochannel),N_("capture channel number")},
	/// "отобразить доступный список каналов"
	{"list-channels",0,NULL,'l',	arg_none,	APTR(&G.listchannels),N_("list avaiable channels")},
	/// "не переходить в фоновый режим"
	{"foreground",0,NULL, 'f',		arg_none,	APTR(&G.nodaemon),	N_("work in foreground")},
	/// "номер порта"
	{"port",	1,	NULL,	'p',	arg_string,	APTR(&G.port),		N_("port number")},
	// ...
	end_option
};


/**
 * Parce command line options and return dynamically allocated structure
 * 		to global parameters
 * @param argc - copy of argc from main
 * @param argv - copy of argv from main
 * @return allocated structure with global parameters
 */
glob_pars *parce_args(int argc, char **argv){
	int i;
	void *ptr;
	ptr = memcpy(&G, &Gdefault, sizeof(G)); assert(ptr);
//	ptr = memcpy(&M, &Mdefault, sizeof(M)); assert(ptr);
//	G.Mirror = &M;
	// format of help: "Usage: progname [args]\n"
	/// "Использование: %s [аргументы]\n\n\tГде аргументы:\n"
	change_helpstring(_("Usage: %s [args]\n\n\tWhere args are:\n"));
	// parse arguments
	parceargs(&argc, &argv, cmdlnopts);
	if(help) showhelp(-1, cmdlnopts);
	if(argc > 0){
		/// "Игнорирую аргумент[ы]:"
		printf("\n%s\n", _("Ignore argument[s]:"));
		for (i = 0; i < argc; i++)
			printf("\t%s\n", argv[i]);
	}
	return &G;
}

