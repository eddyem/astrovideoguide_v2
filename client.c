/*
 * client.c
 *
 * Copyright 2015 Edward V. Emelianov <eddy@sao.ru, edward.emelianoff@gmail.com>
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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>

#include "usefull_macros.h"
#include "parceargs.h"

#define BUFSIZE  (20480)

/*
 * here are some typedef's for global data
 */

typedef struct{
	int istart;     // starting number
	int nframes;    // amount of frames to capture
	char *host;     // host to connect
	char *port;     // port of socket
	char *format;   // image format
}glob_pars;

glob_pars *parce_args(int argc, char **argv);

/*
 * here are global parameters initialisation
 */
glob_pars G;  // internal global parameters structure
int help = 0; // whether to show help string

glob_pars Gdefault = {
	.istart = 1,
	.nframes = 3,
	.host = "localhost",
	.port = "54321",
	.format = "jpg"
};

/*
 * Define command line options by filling structure:
 *	name	has_arg	flag	val		type		argptr			help
*/
myoption cmdlnopts[] = {
	/// "отобразить это сообщение"
	{"help",	0,	NULL,	'h',	arg_int,	APTR(&help),		N_("show this help")},
	/// "путь к устройству видеозахвата"
	{"istart",	1,	NULL,	's',	arg_int,	APTR(&G.istart),	N_("starting frame number")},
	/// "номер канала захвата"
	{"nframes", 1,	NULL,	'N',	arg_int,	APTR(&G.nframes),	N_("amount of frames to capture")},
	{"hostname",1,	NULL,	'h',	arg_string,	APTR(&G.host),		N_("hostname of server")},
	{"port",	1,	NULL,	'p',	arg_string,	APTR(&G.port),		N_("port to connect")},
	{"format",	1,	NULL,	'f',	arg_string,	APTR(&G.format),	N_("image format (raw/png/jpg)")},
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


/**
 * Test function to save captured frame to a ppm file
 * @param pFrame - pointer to captured frame
 * @param iFrame - frame number (for filename like frameXXX.png
 * @return 0 if false
 *
 * image format:
 * format\nsize\ndata
 */
int SaveFrame(uint8_t *pFrame, size_t sz, int iFrame){
	int F;
	char Filename[32], *eptr;
	long L;
	snprintf(Filename, 31, "frame%03d.%s", iFrame, G.format);
	F = open(Filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if(F < 0){
		WARN("open(%s)", Filename);
		return 0;
	}
	if(strncasecmp(G.format, (char*)pFrame, strlen(G.format))){
		WARNX("Wrong format in answer!");
		return 0;
	}
	pFrame = (uint8_t*)strchr((char*)pFrame, '\n');
	if(!pFrame || !(++pFrame)){
		WARNX("bad file!");
		return 0;
	}
	L = strtol((char*)pFrame, &eptr, 10);
	if(!eptr || *eptr != '\n'){
		WARNX("bad file!");
		return 0;
	}
	++eptr;
	sz = (size_t)L;
	if((size_t)write(F, eptr, sz) != sz){
		WARN("write");
		return 0;
	}
	if(close(F)){
		WARN("close");
		return 0;
	}
	/// "Кадр сохранен"
	green("%s\n", _("Frame saved"));
	return 1;
}

/**
 * wait for answer from server
 * @param sock - socket fd
 * @return 0 in case of error or timeout, 1 in case of socket ready
 */
int waittoread(int sock){
	fd_set fds;
	struct timeval timeout;
	int rc;
	timeout.tv_sec = 0; // wait not more than 100 millisecond
	timeout.tv_usec = 100000;
	FD_ZERO(&fds);
	FD_SET(sock, &fds);
	rc = select(sock+1, &fds, NULL, NULL, &timeout);
	if(rc < 0){
		perror("select failed");
		return 0;
	}
	if(FD_ISSET(sock, &fds)){
		DBG("there's data in socket");
		return 1;
	}
	return 0;
}

int sockfd = 0;
int open_socket(){
	struct addrinfo h, *r, *p;
	memset(&h, 0, sizeof(h));
	h.ai_family = AF_INET;
	h.ai_socktype = SOCK_STREAM;
	h.ai_flags = AI_CANONNAME;
	char *host = G.host;
	char *port = G.port;
	if(getaddrinfo(host, port, &h, &r)){perror("getaddrinfo"); return 1;}
	struct sockaddr_in *ia = (struct sockaddr_in*)r->ai_addr;
	char str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(ia->sin_addr), str, INET_ADDRSTRLEN);
	printf("canonname: %s, port: %u, addr: %s\n", r->ai_canonname, ntohs(ia->sin_port), str);
	for(p = r; p; p = p->ai_next) {
		if((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1){
			perror("socket");
			continue;
		}
		if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1){
			close(sockfd);
			perror("connect");
			continue;
		}
		break; // if we get here, we must have connected successfully
	}
	if(p == NULL){
		// looped off the end of the list with no connection
		fprintf(stderr, "failed to connect\n");
		return 1;
	}
	freeaddrinfo(r);
	//setnonblocking(sockfd);
	return 0;
}

uint8_t *capture_frame(size_t *sz){
	size_t bufsz = BUFSIZE;
	uint8_t *recvBuff = MALLOC(uint8_t, bufsz);
	char *msg = G.format;
	size_t L = strlen(msg);
	ssize_t LL = write(sockfd, msg, L);
	if((size_t)LL != L){perror("send"); return NULL;}
	DBG("send %s (len=%zd) to fd=%d", msg, L, sockfd);
	if(!waittoread(sockfd)){
		DBG("Nothing to read");
		return NULL;
	}
	size_t offset = 0;
	do{
		if(offset >= bufsz){
			bufsz += BUFSIZE;
			recvBuff = realloc(recvBuff, bufsz);
			assert(recvBuff);
			DBG("Buffer reallocated, new size: %zd\n", bufsz);
		}
		LL = read(sockfd, &recvBuff[offset], bufsz - offset);
		if(!LL) break;
		if(LL < 0){
			perror("read");
			return NULL;
		}
		offset += (size_t)LL;
	}while(waittoread(sockfd));
	if(!offset){
		fprintf(stderr, "Socket closed\n");
		return NULL;
	}
	printf("read %zd bytes\n", offset);
	if(sz) *sz = offset;
	return recvBuff;
}

/**
 * Read N frames and save them to disk
 * @param istart - started image number (for saving)
 * @param N      - number of frames
 * @return number of saved frames
 */
int capture_frames(int istart, int N){
	int i, saved = 0;
	size_t S;
	uint8_t *pic = NULL;
	for(i = 0; i < N; i++){
		if((pic = capture_frame(&S))){
			SaveFrame(pic, S, istart + saved++);
			FREE(pic);
		}
	}
	return saved;
}

int main(int argc, char **argv){
	initial_setup();
	parce_args(argc, argv);
	// test format
	if((strcasecmp(G.format, "png") != 0) && (strcasecmp(G.format, "raw") != 0)
		&& (strcasecmp(G.format, "jpg") != 0)){
		WARNX("Wrong format, should be one of raw/png/jpg!");
		return -1;
	}
	if(open_socket()) ERRX(_("Can't open socket!"));
	printf("Capture %d frames starting from %d\n", G.nframes, G.istart);
	capture_frames(G.istart, G.nframes);
	close(sockfd);
	return 0;
}
