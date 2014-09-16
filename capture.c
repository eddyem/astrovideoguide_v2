/*
 * capture.c
 *
 * Copyright 2014 Edward V. Emelianov <eddy@sao.ru, edward.emelianoff@gmail.com>
 * Written with parts of ffmpeg tutorial by Stephen Dranger (dranger@gmail.com)
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
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/videodev2.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>

#include <stdio.h>

#include "main.h"

// global variables for capture_frame
int videoStream;
AVFormatContext *pFormatCtx = NULL;
uint8_t *buffer = NULL;
AVFrame *pFrame = NULL;
AVFrame *pFrameRGB = NULL;
AVCodecContext *pCodecCtx = NULL;
struct SwsContext *sws_ctx = NULL;
// flag saying that device is ready
int videodev_prepared = 0;



/**
 * Test function to save captured frame to a ppm file
 * @param pFrame - pointer to captured frame
 * @param iFrame - frame number (for filename like frameXXX.png
 */
void SaveFrame(AVFrame *pFrame, int iFrame){
	FILE *pFile;
	char szFilename[32];
	int  y, w = pFrame->width, h = pFrame->height, l = pFrame->linesize[0];
	sprintf(szFilename, "frame%03d.ppm", iFrame);
	pFile=fopen(szFilename, "w");
	if(pFile == NULL) return;
	fprintf(pFile, "P6\n%d %d\n255\n", w, h);
	for(y = 0; y < h; y++)
		fwrite(pFrame->data[0]+y*l, 1, w*3, pFile);
	fclose(pFile);
	/// "Кадр сохранен"
	green("%s\n", _("Frame saved"));
}

/**
 * Check whether input with number ch_num is available
 * @param fd     - file descriptor for videodev
 * @param ch_num - number of channel
 * @return 0 if false
 */
int check_input(int fd, int ch_num){
	struct v4l2_input vin;
	vin.index = ch_num;
	if(ioctl(fd, VIDIOC_ENUMINPUT, &vin) == -1){
		return 0;
	}
	return 1;
}

/**
 * Check whether input with number ch_num is available and if available, lists its controls
 * if channel is not available, function shows error message
 * @param fd     - file descriptor for videodev
 * @param ch_num - number of channel
 * @return 0 if false
 */
int list_input(int fd, int ch_num){
	struct v4l2_input vin;
	vin.index = ch_num;
	DBG("ioctl: VIDIOC_ENUMINPUT\n");
	if(ioctl(fd, VIDIOC_ENUMINPUT, &vin) == -1){
		/// "Нет такого канала"
		WARN(_("No such channel"));
		return 0;
	}
	printf("\tInput       : %d\n", vin.index);
	printf("\tName        : %s\n", vin.name);
	printf("\tType        : 0x%08X\n", vin.type);
	printf("\tAudioset    : 0x%08X\n", vin.audioset);
	printf("\tTuner       : 0x%08X\n", vin.tuner);
	printf("\tStandard    : 0x%016llX\n", (unsigned long long)vin.std);
	printf("\tStatus      : 0x%08X\n", vin.status);
	printf("\tCapabilities: 0x%08X\n", vin.capabilities);
	return 1;
}

/**
 * Lists all available inputs for given device
 * @param dev - video defice file name
 */
void list_all_inputs(char *dev){
	int i = 0, l, fd;
	fd = open(dev, O_RDWR | O_NONBLOCK, 0);
	if(fd < 0) return;
	do{
		l = check_input(fd, i);
		if(l) list_input(fd, i++);
		printf("\n");
	}while(l);
}

/**
 * Set active grab channel
 * @param devname - name of video device
 * @param ch_num  - channel number
 * @return 0 if failure
 */
int grab_set_chan(char *devname, int ch_num){
	int  grab_fd;
	int input;
	if(!devname) exit(EXIT_FAILURE);
	struct stat st;
	if(-1 == stat(devname, &st)){
		/// "Не могу идентифицировать"
		WARN("%s '%s'", _("Cannot identify"), devname);
		return 0;
	}
	if(!S_ISCHR(st.st_mode)){
		/// "не является символьным устройством"
		WARNX("'%s' %s\n", devname, _("is not character device file"));
		return 0;
	}
	grab_fd = open(devname, O_RDWR | O_NONBLOCK, 0);
	if(-1 == grab_fd){
		/// "Не могу открыть"
		WARN("%s '%s'", _("Cannot open"), devname);
		return 0;
	}
	if(ioctl(grab_fd, VIDIOC_G_INPUT, &input) == 0){
		/// "Текущий вход"
		printf("%s: %d\n", _("Current input"), input);
	}
	if(input != ch_num){
		if(list_input(grab_fd, ch_num)){
			if(-1 == ioctl (grab_fd, VIDIOC_S_INPUT, &ch_num)){
				/// "Не могу выбрать требуемый канал"
				WARN(_("Can't set given channel"));
				return 0;
			}
		}else{
			return 0;
		}
	}
	close(grab_fd);
	return 1;
}

/**
 * Prepare video device to capture
 * @param videodev - device file name
 * @param channel  - number of channel
 * @return 0 if failure
 */
int prepare_videodev(char *videodev, int channel){
	int i, numBytes, averr;
	AVCodec *pCodec = NULL;
	AVInputFormat* ifmt;
	AVDictionary *optionsDict = NULL;
	char averrbuf[256];

	#ifdef EBUG
	av_log_set_level(AV_LOG_DEBUG);
	#else
	av_log_set_level(AV_LOG_WARNING);
	#endif
	// Register all formats and codecs
	av_register_all();
	avdevice_register_all();
	// find v4l2 format support
	ifmt = av_find_input_format("video4linux2");
	if(!ifmt){
		/// "Не могу найти поддержку v4l2!"
		WARNX("%s\n", _("Can't find v4l2 support!"));
		return 0;
	}
//  av_dict_set(&optionsDict, "analyzeduration", "0", 0);

	// set channel
	if(!grab_set_chan(videodev, channel)){
		return 0;
	}

	// Open video file
	if((averr = avformat_open_input(&pFormatCtx, videodev, ifmt, &optionsDict)) < 0){
		av_strerror(averr, averrbuf, 255);
		/// "Не могу открыть устройство"
		WARNX("%s %s! (%s)\n", _("Can't open device"), videodev, averrbuf);
		return 0; // Couldn't open file
	}

	// Retrieve stream information
	if((averr = avformat_find_stream_info(pFormatCtx, NULL) < 0)){
		av_strerror(averr, averrbuf, 255);
		/// "Не могу обнаружить информацию о потоке"
		WARNX("%s: %s!", _("Can't find stream information"), averrbuf);
		return 0; // Couldn't find stream information
	}

	// Dump information about file onto standard error
	av_dump_format(pFormatCtx, 0, videodev, 0);

	// Find the first video stream
	videoStream = -1;
	int nofstreams = pFormatCtx->nb_streams;
	for(i = 0; i < nofstreams; i++)
		if(pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO){
			videoStream = i;
			break;
		}
	if(videoStream == -1){
		/// "Не могу найти видеопоток!"
		WARNX("Can't find video stream!");
		return 0; // Didn't find a video stream
	}

	// Get a pointer to the codec context for the video stream
	pCodecCtx=pFormatCtx->streams[videoStream]->codec;

	// Find the decoder for the video stream
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if(pCodec == NULL){
		/// "Не поддерживаемый кодек!"
		WARNX("Unsupported codec!");
		return 0; // Codec not found
	  }
	// Open codec
	if((averr = avcodec_open2(pCodecCtx, pCodec, &optionsDict)) < 0){
		av_strerror(averr, averrbuf, 255);
		/// "Не могу открыть кодек!"
		WARNX("%s: %s!", _("Can't open codec!"), averrbuf);
		return 0; // Could not open codec
	}

	// Allocate video frame
	pFrame = av_frame_alloc();
	assert(pFrame != NULL);

	// Allocate an AVFrame structure
	pFrameRGB = av_frame_alloc();
	assert(pFrameRGB != NULL);

	// Determine required buffer size and allocate buffer
	numBytes = avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width,
			pCodecCtx->height);
	buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
	assert(buffer != NULL);

	sws_ctx = sws_getContext(
		pCodecCtx->width,
		pCodecCtx->height,
		pCodecCtx->pix_fmt,
		pCodecCtx->width,
		pCodecCtx->height,
		PIX_FMT_RGB24,
		SWS_BILINEAR,
		NULL,
		NULL,
		NULL
	);
	assert(sws_ctx != NULL);

	// Assign appropriate parts of buffer to image planes in pFrameRGB
	// Note that pFrameRGB is an AVFrame, but AVFrame is a superset
	// of AVPicture
	avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_RGB24,
		 pCodecCtx->width, pCodecCtx->height);
	videodev_prepared = 1;
	return 1;
}

/**
 * Capture next frame
 * @return 0 on error
 */
int capture_frame(int istart, int N){
	if(!videodev_prepared){
		/// "Видеоустройство не было инициализировано функцией prepare_videodev"
		WARNX("Video device wasn't prepared with prepare_videodev");
		return 0;
	}
	// Read frames and save first five frames to disk
	int frameFinished, ilast = istart + N;
	AVPacket packet;

	while(av_read_frame(pFormatCtx, &packet) >= 0) {
	// Is this a packet from the video stream?
		if(packet.stream_index == videoStream){
			// Decode video frame
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished,
					&packet);
			// Did we get a video frame?
			if(frameFinished){
				// Convert the image from its native format to RGB
				sws_scale(
					sws_ctx,
					(uint8_t const * const *)pFrame->data,
					pFrame->linesize,
					0,
					pCodecCtx->height,
					(uint8_t *const *)pFrameRGB->data,
					pFrameRGB->linesize
				);
				// fill frame size fields
				pFrameRGB->width = pCodecCtx->width;
				pFrameRGB->height = pCodecCtx->height;
				// Save the frame to disk
				if(istart < ilast)
					SaveFrame(pFrameRGB, istart);
				if(++istart >= ilast) break;
			}
		}
		// Free the packet that was allocated by av_read_frame
		av_free_packet(&packet);
	}

	return 1;
}

/**
 * Free unused memory
 */
void free_videodev(){
	if(!videodev_prepared) return; // nothing to do
	// Free the RGB image
	if(buffer) av_free(buffer);
	if(pFrameRGB) av_free(pFrameRGB);
	// Free the YUV frame
	if(pFrame) av_free(pFrame);
	// Close the codec
	if(pCodecCtx) avcodec_close(pCodecCtx);
	// free context
	if(sws_ctx) sws_freeContext(sws_ctx);
	// Close the video file
	if(pFormatCtx) avformat_close_input(&pFormatCtx);
	videodev_prepared = 0;
}
