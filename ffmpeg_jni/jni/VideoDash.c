/*
 * VideoDash.c
 *
 *  Created on: 14 abr. 2019
 *      Author: brad
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "ffmpeg_jni_VideoDash.h"
#include "ffmpeg_jni_VideoThumbnail.h"
#include "VideoDash.h"

void printusage();

int main(int argc,char* argv[]){
	char filename[256];
	if(argc<2 || argc>=3){
		printusage();
		return -1;
	}
	strcpy(filename,argv[1]);
	return getVideoDash(filename);

}

void printusage(){
	printf("Uso: VideoDash nombrearchivo\n");
}
