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
// for pthread_kill
#define _XOPEN_SOURCE  666
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

// daemon.c
extern void check4running(char **argv, char *pidfilename, void (*iffound)(pid_t pid));

// Max amount of connections
#define BACKLOG     30

#define BUFLEN (1024)

glob_pars *Global_parameters = NULL;

pthread_mutex_t readout_mutex = PTHREAD_MUTEX_INITIALIZER;
uint8_t *frame = NULL;
int w, h;

typedef enum{
	IMTYPE_NONE = 0,
	IMTYPE_RAW,
	IMTYPE_JPG,
	IMTYPE_PNG
} imagetype;

// first jpeg added for ability of writing imsuffixes[imtype]
static const char *imsuffixes[] = { "jpeg", "raw", "jpg", "png", NULL };
static const char *mimetypes[] = { "jpeg", "raw", "jpeg", "png"};
static const imagetype suffixtypes[] = { IMTYPE_JPG, IMTYPE_RAW, IMTYPE_JPG, IMTYPE_PNG };


static volatile int global_quit = 0;
// quit by signal
static void signals(_U_ int sig){
	DBG("Get signal %d, quit.\n", sig);
	global_quit = 1;
	sleep(1);
	exit(sig);
}

static volatile uint64_t imctr = 0; // frame counter (we need it to know that there's some new frames)
void *read_buf(_U_ void *buf){
	while(!global_quit){
		if(!videodev_prepared){
			if(!prepare_videodev(Global_parameters->videodev, Global_parameters->videochannel)){
				int i;
				char buf[128];
				for(i = 0; i < 256; ++i){
					snprintf(buf, 128, "/dev/video%d", i);
					if(prepare_videodev(buf, Global_parameters->videochannel))
						break;
				}
				if(i == 256){
					/// "�� ���� ����������� ��������������� � ������"
					ERR(_("Can't prepare video device"));
				}
			}
		}
		pthread_mutex_lock(&readout_mutex);
		uint8_t *capt;
		if((capt = capture_frame(&w, &h))){
			imctr++;
			FREE(frame);
			size_t s = w*h;
			frame = MALLOC(uint8_t, s);
			memcpy(frame, capt, s);
			//DBG("imctr: %zd", imctr);
		}
		pthread_mutex_unlock(&readout_mutex);
		usleep(100);
	}
	return NULL;
}

// search a first word after needle without spaces
char* stringscan(char *str, char *needle){
	char *a, *e;
	char *end = str + strlen(str);
	a = strstr(str, needle);
	if(!a) return NULL;
	a += strlen(needle);
	while (a < end && (*a == ' ' || *a == '\r' || *a == '\t' || *a == '\r')) a++;
	if(a >= end) return NULL;
	e = strchr(a, ' ');
	if(e) *e = 0;
	return a;
}

/**
 * Send image to user
 * @param strip  - ==1 to send image without info headers
 *                 ==0 to send in form "format\nsize\ndata", where
 *                     format - one of "raw", "jpg", "png"
 *                     size is binary file size (in jpg/png formats) or image size in pixels (in raw)
 * @param imtype - image type
 * @param sockfd - socket fd for sending data
 */
void send_image(int strip, imagetype imtype, int sockfd){
	if(imtype == IMTYPE_NONE) return;
	char buf[1024];
	uint8_t *buff = NULL, *imagedata = NULL;
	size_t buflen = 0, L;
	ssize_t sent;
	// make image file
	pthread_mutex_lock(&readout_mutex);
	// convert frame[w x h] into requested format
	switch(imtype){
		case IMTYPE_JPG:
			imagedata = getjpg(&buflen, w, h, frame);
		break;
		case IMTYPE_PNG:
			imagedata = getpng(&buflen, w, h, frame);
		break;
		case IMTYPE_RAW:
			buflen = w*h;
			imagedata = MALLOC(uint8_t, buflen);
			if(imagedata) memcpy(imagedata, frame, buflen);
		break;
		default:
			return;
	}
	pthread_mutex_unlock(&readout_mutex);
	if(!imagedata) return;
	if(!strip){
		if(imtype == IMTYPE_RAW)
			L = snprintf(buf, 255, "%s\n%dx%d\n", imsuffixes[imtype], w, h);
		else
			L = snprintf(buf, 255, "%s\n%zd\n", imsuffixes[imtype], buflen);
	}else{
		L = snprintf(buf, 1023, "HTTP/2.0 200 OK\r\nContent-type: image/%s\r\n"
			"Content-Length: %zd\r\n\r\n", mimetypes[imtype], buflen);
	}
	buff = MALLOC(uint8_t, L + buflen);
	memcpy(buff, buf, L);
	memcpy(buff+L, imagedata, buflen);
	FREE(imagedata);
	buflen += L;
	sent = write(sockfd, buff, buflen);
	//DBG("send %ld bytes\n", sent);
	if((size_t)sent != buflen) WARN("write()");
	FREE(buff);
}

int myatoi(char *str, int *iret){
	long tmp;
	char *endptr;
	tmp = strtol(str, &endptr, 0);
	if(endptr == str || *str == '\0' || *endptr != '\0')
		return 0;
	*iret = (int) tmp;
	return 1;
}

void *handle_socket(void *asock){
	//FNAME();
	if(global_quit) return NULL;
	int sock = *((int*)asock);
	int webquery = 0; // whether query is web or regular
	static uint64_t oldimctr = 0;
	char buff[BUFLEN+1], *bufptr;
	imagetype imtype = IMTYPE_NONE;
	ssize_t readed;
	while(!global_quit){
		bufptr = buff;
		// fill incoming buffer
		readed = read(sock, buff, BUFLEN);
		if(readed <= 0){ // error or disconnect
			DBG("Nothing to read from fd %d (ret: %zd)", sock, readed);
			break;
		}
		bufptr += readed;
		// add trailing zero to be on the safe side
		*bufptr = 0;
	//	DBG("get %zd bytes: %s", readed, buff);
		// now we should check what do user want
		char *got, *found = NULL;
		DBG("Buff: %s", buff);
		if((got = stringscan(buff, "GET")) || (got = stringscan(buff, "POST")) ||
			(got = stringscan(buff, "PUT"))){ // web query
			webquery = 1;
			//DBG("Web query:\n%s\n", got);
			// web query have format GET /anyname.suffix, where suffix defines file type
			if(!(found = strstr(got, "sum="))){
				if(*got != '/')
					break;
				if(!(found = strrchr(got, '.')) || !(found[1]))
					break;
				++found;
			}
		}else{ // regular query
			found = buff;
		}
		//DBG("message: %s", found);
		if(strncmp(found, "sum=", 4) == 0){
				int x;
				if(myatoi(found + 4, &x)){
					if(x > 0 && x < 255)
						Global_parameters->nsum = x;
				}
				size_t sumlen = snprintf(buff, BUFLEN, "sum=%d", Global_parameters->nsum);
				size_t L = snprintf(buff, BUFLEN,
					"HTTP/2.0 200 OK\r\n"
					"Access-Control-Allow-Origin: *\r\n"
					"Access-Control-Allow-Methods: GET, POST\r\n"
					"Access-Control-Allow-Credentials: true\r\n"
					"Content-type: multipart/form-data\r\nContent-Length: %zd\r\n\r\n"
					"sum=%d", sumlen,
					Global_parameters->nsum);
				if(L != (size_t)write(sock, buff, L)) perror("write");
				DBG("%s", buff);
				break;
			}
		int i = 0;
		imtype = IMTYPE_NONE;
		do{
			if(strcasecmp(found, imsuffixes[i]) == 0){
				imtype = suffixtypes[i];
				break;
			}
		}while(imsuffixes[++i]);
		// OK, now we now what user want. Send to him his image file
		while(oldimctr == imctr); // wait for buffer update
		oldimctr = imctr;
		send_image(webquery, imtype, sock);
		if(webquery) break; // close connection if this is a web query
	}
	close(sock);
	//DBG("closed");
	pthread_exit(NULL);
	return NULL;
}

static inline void main_proc(){
	pthread_t readout_thread;
	int sock;
	struct addrinfo hints, *res, *p;
	int reuseaddr = 1;
	if(pthread_create(&readout_thread, NULL, read_buf, NULL)){
		/// "�� ���� ������� ����� ��� ������� �����"
		ERR(_("Can't create readout thread"));
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if(getaddrinfo(NULL, Global_parameters->port, &hints, &res) != 0){
		ERR("getaddrinfo");
	}
	struct sockaddr_in *ia = (struct sockaddr_in*)res->ai_addr;
	char str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(ia->sin_addr), str, INET_ADDRSTRLEN);
	DBG("port: %u, addr: %s\n", ntohs(ia->sin_port), str);
	// loop through all the results and bind to the first we can
	for(p = res; p != NULL; p = p->ai_next){
		if((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
			WARN("socket");
			continue;
		}
		if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int)) == -1){
			ERR("setsockopt");
		}
		if(bind(sock, p->ai_addr, p->ai_addrlen) == -1){
			close(sock);
			WARN("bind");
			continue;
		}
		break; // if we get here, we have a successfull connection
	}
	if(p == NULL){
		// looped off the end of the list with no successful bind
		ERRX("failed to bind socket");
	}
	// Listen
	if(listen(sock, BACKLOG) == -1){
		ERR("listen");
	}
	freeaddrinfo(res);
	// Main loop
	while(!global_quit){
		fd_set readfds;
		struct timeval timeout;
		socklen_t size = sizeof(struct sockaddr_in);
		struct sockaddr_in their_addr;
		int newsock;
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);
		timeout.tv_sec = 0; // wait not more than 10 milliseconds
		timeout.tv_usec = 10000;
		int sel = select(sock + 1 , &readfds , NULL , NULL , &timeout);
		if(sel < 0){
			if(errno != EINTR)
				WARN("select()");
			continue;
		}
		if(!(FD_ISSET(sock, &readfds))) continue;
	//	DBG("accept");
		newsock = accept(sock, (struct sockaddr*)&their_addr, &size);
		if(newsock <= 0){
			WARN("accept()");
			continue;
		}
		pthread_t handler_thread;
		if(pthread_create(&handler_thread, NULL, handle_socket, (void*) &newsock))
			WARN("pthread_create()");
		else
			pthread_detach(handler_thread);
		if(pthread_kill(readout_thread, 0) == ESRCH) // the readout thread is dead
			break;
	}

	if(!global_quit){ // some error occured
		pthread_mutex_lock(&readout_mutex);
		if(pthread_cancel(readout_thread)){
			WARN("Can't cancel readout thread!");
		}
	}
	// wait for thread ends before closing videodev
	pthread_join(readout_thread, NULL);
	pthread_mutex_unlock(&readout_mutex);
	close(sock);
	free_videodev();
}

int main(int argc, char **argv){
	// setup coloured output
	initial_setup();
	check4running(argv, NULL, NULL);
	/*
	 * To run in GUI bullshit (like qt or gtk) you must do:
	 * bind_textdomain_codeset(PACKAGE, "UTF8");
	 */
	Global_parameters = parce_args(argc, argv);
	assert(Global_parameters != NULL);
	DBG("videodev: %s, channel: %d\n", Global_parameters->videodev, Global_parameters->videochannel);
	DBG("list: %d\n", Global_parameters->listchannels);
	if(Global_parameters->listchannels){
		list_all_inputs(Global_parameters->videodev);
		return 0;
	}

	signal(SIGTERM, signals); // kill (-15) - quit
	signal(SIGHUP, SIG_IGN);  // hup - ignore
	signal(SIGINT, signals);  // ctrl+C - quit
	signal(SIGQUIT, signals); // ctrl+\ - quit
	signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z

#ifndef EBUG // daemonize only in release mode
	if(!Global_parameters->nodaemon){
		if(daemon(1, 0)){
			perror("daemon()");
			exit(1);
		}
	}
#endif // EBUG
	while(1){
		pid_t childpid = fork();
		if(childpid){
			DBG("Created child with PID %d\n", childpid);
			wait(NULL);
			printf("Child %d died\n", childpid);
		}else{
			prctl(PR_SET_PDEATHSIG, SIGTERM); // send SIGTERM to child when parent dies
			main_proc();
			return 0;
		}
	}
	return 0;
}
