/*
 * ffmpeg_jni_VideoDash.c
 *
 *  Created on: 8 abr. 2019
 *      Author: Jorge Nuñez Rivera
 */
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>

#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include "ffmpeg_jni_VideoThumbnail.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

static void logging(const char *fmt, ...);
int getVideoDash( const char* inputFileName);

JNIEXPORT jint JNICALL Java_ffmpeg_1jni_VideoDash_getVideoDash
(JNIEnv *env, jobject obj, jstring jFilename){
	const char* fileName=(*env)->GetStringUTFChars(env,jFilename,NULL);
	return getVideoDash(fileName);
}

int getVideoDash(const char* inputFileName){
	//ENTRADA
	AVFormatContext *inFCtx=NULL;	//DEMUXER
	AVCodecContext *inACCtx=NULL;	//VIDEO DECO
	AVCodecContext *inVCCtx=NULL;	///AUDIO DECO
	AVStream *inAStream=NULL;
	AVStream *inVStream=NULL;
	AVPacket *inPacket=NULL;
	AVFrame *inFrame=NULL;
	int inVBitrate=0;
	int inVWidth=0;
	int inVHeight=0;
	int inAChannels=0;
	//SALIDA
	AVFormatContext *outFCtx=NULL;	//MUXER
	AVOutputFormat *outFmt=NULL;
	AVCodecContext *outACCtx=NULL;
	AVCodecContext *outVLowCCtx=NULL;
	AVCodecContext *outVMidCCtx=NULL;
	AVCodecContext *outVHighCCtx=NULL;
	AVStream *outAStream=NULL;
	AVStream *outVLowStream=NULL;
	AVStream *outVMidStream=NULL;
	AVStream *outVHighStream=NULL;
	AVPacket *outPacket=NULL;
	AVFrame *outFrame=NULL;
	//ESCALADO
	struct SwsContext *sclMidCtx=NULL;
	struct SwsContext *sclLowCtx=NULL;

	int response=0;
	int frame_index=0;
	int outAChannels=2;//2 por defecto, si la entrada es mono 1

	//ENTRADA *********************************************************ENTRADA**************************************************************************

	//Reservar memoria para FORMAT context
	inFCtx = avformat_alloc_context();
	if (!inFCtx) {
		logging("ERROR could not allocate memory for Format Context");
		return -1;
	}
	//Cargar el archivo de entrada
	response=avformat_open_input(&inFCtx, inputFileName, NULL, NULL);
	if (response<0) {
		logging("ERROR could not open input file");
		return response;
	}
	//Carga la información de los streams
	response=(avformat_find_stream_info(inFCtx,  NULL));
	if  (response < 0) {
		logging("ERROR could not get the stream info");
		return response;
	}
	//Codec y parametros de los streams
	int best_video_stream_index = -1;
	int best_audio_stream_index = -1;
	// para cada stream comprobar si es de video o audio, guardar el mejor de cada
	for (int i = 0; i < inFCtx->nb_streams; i++)
	{
		AVCodecParameters *codecPars =  NULL;
		codecPars = inFCtx->streams[i]->codecpar;
		if(codecPars->codec_type==AVMEDIA_TYPE_VIDEO){
			if(best_video_stream_index == -1||codecPars->bit_rate>inFCtx->streams[best_video_stream_index]->codecpar->bit_rate){
				best_video_stream_index=i;
			}
		}else if(codecPars->codec_type==AVMEDIA_TYPE_AUDIO){
			if(best_audio_stream_index == -1||codecPars->bit_rate>inFCtx->streams[best_audio_stream_index]->codecpar->bit_rate){
				best_audio_stream_index=i;
			}
		}else{
			continue;
		}
	}
	if(best_audio_stream_index==-1){
		logging("Advertencia! no se ha encontrado un apista de audio");
	}
	if(best_video_stream_index==-1){
		logging("Error! no se ha encontrado ninguna pista de video");
		return -1;
	}
	inAStream = inFCtx->streams[best_audio_stream_index];
	AVCodec* decoA = avcodec_find_decoder(inAStream->codecpar->codec_id);
	if(decoA==NULL){
		logging("No se ha encontrado codec para decodificar el audio");
		return -1;
	}
	inACCtx= avcodec_alloc_context3(decoA);
	if(!inACCtx){
		logging("No se ha podido reservar memoria para el decoder de audio");
		return -1;
	}
	response = avcodec_parameters_to_context(inACCtx, inAStream->codecpar);
	if(response<0){
		logging("No se ha podido copiar los parametros para el  decoder de audio: %s", av_err2str(response));
		return -1;
	}
	response=avcodec_open2(inACCtx,decoA,NULL);
	if(response<0){
		logging("imposible abrir codec de entrada de audio: %s",av_err2str(response));
	}
	inAChannels=inACCtx->channels;
	if(inAChannels==1){
		outAChannels=1;
	}else if(inAChannels<1){
		logging("No hay canales de audio!!!!");//POR RESOLVER
		return -1;
	}

	inVStream = inFCtx->streams[best_video_stream_index];
	AVCodec* decoV = avcodec_find_decoder(inVStream->codecpar->codec_id);
	inVCCtx= avcodec_alloc_context3(decoV);
	if(!inVCCtx){
		logging("No se ha podido reservar memoria para el decoder de video");
		return -1;
	}
	response = avcodec_parameters_to_context(inVCCtx, inVStream->codecpar);
	if(response<0){
		logging("No se ha podido copiar los parametros para el  decoder de video: %s", av_err2str(response));
		return -1;
	}
	inVBitrate=inVStream->codecpar->bit_rate;
	inVWidth=inVStream->codecpar->width;
	inVHeight=inVStream->codecpar->height;

	//SALIDA*******************************************************************SALIDA*******************************************************************
	//************************************************************************************************************************************************
	//Nombre del archivo de salida
	char out_dir[256]="";
	strcpy(out_dir,inputFileName);
	char* lastdot = strrchr(out_dir,'.');
	lastdot[0]='\0';
	strcat(out_dir,"-DASH");
	char out_file[256]="";
	strcpy(out_file,out_dir);
	strcat(out_file,"/stream.mpd");
	struct stat st = {0};
	if (stat(out_dir, &st) == -1) { //Si no existe el directorio, crearlo
	    mkdir(out_dir, 0700);
	} //SI YA EXISTE: ERROR????
	getVideoThumb(inputFileName);

	//Cargar el archivo de salida, como output del FormatContext;
	response=avformat_alloc_output_context2(&outFCtx, NULL, NULL, out_file);
	if (!outFCtx||response<0) {
		logging("Could not create output context\n");
		return response;
	}
	outFmt = av_guess_format("dash", NULL, NULL);
	outFCtx->oformat = outFmt;


	//OUT STREAMS & CODECS*****************************************STREAMS*******************************************************************************


	//**************************************************************AUDIO****************************************************************************************

	AVCodec *audioEnc=avcodec_find_encoder_by_name("aac");
	if(audioEnc==NULL){
		logging("codec not found for (aac)");
		return -1;
	}
	outAStream = avformat_new_stream(outFCtx, audioEnc);
	if (!outAStream) {
		logging("Failed allocating audio output stream.");
		return AVERROR_UNKNOWN;
	}
	outACCtx = avcodec_alloc_context3(audioEnc);
	outACCtx->codec_type = AVMEDIA_TYPE_AUDIO;
	outACCtx->sample_fmt = audioEnc->sample_fmts[0];
	outACCtx->sample_rate= inACCtx->sample_rate;

	outACCtx->compression_level=1;
	outACCtx->channels =outAChannels;
	if(outAChannels>1){
		outACCtx->channel_layout=AV_CH_LAYOUT_STEREO;
	}else{
		outACCtx->channel_layout=AV_CH_LAYOUT_MONO;
	}
	outACCtx->bit_rate = inACCtx->bit_rate;
	outACCtx->time_base = (AVRational){1, 25};
	outACCtx->profile=FF_PROFILE_AAC_LOW;
	outACCtx->frame_size =inACCtx->frame_size;
	if (outFCtx->oformat->flags & AVFMT_GLOBALHEADER)
		outACCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	uint8_t extradataaudio[5]={17,144,86,229,1}; //17 is mp4a.40.2 profile, 144 is 2 channel  {17,144,86,66,229} funciona
	outACCtx->extradata_size=5;
	outACCtx->extradata=extradataaudio;
	outACCtx->codec_tag =  MKTAG('m', 'p', '4', 'a');
	avcodec_parameters_from_context(outAStream->codecpar,outACCtx);
	//***************************************VIDEO  ******************************************************************************************
	//*************************************CODEC (COMUN) **************************************************************************************
	AVCodec* videoEnc=avcodec_find_encoder(AV_CODEC_ID_H264);   //AV_CODEC_ID_H264)
	if(videoEnc==NULL){
		logging("codec not found for (h264)");
		return -1;
	}

	//************************************ PERFIL BAJO **************************************************************************************
	outVLowStream = avformat_new_stream(outFCtx, videoEnc);
	if (!outVLowStream) {
		logging("Failed allocating video low output stream.");
		return AVERROR_UNKNOWN;
	}
	outVLowCCtx = avcodec_alloc_context3(videoEnc);
	if(outVLowCCtx==NULL){
		logging("Failed allocating video low output codec context");
		return -1;
	}
	outVLowCCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	//outVLowCCtx->bit_rate = inVBitrate*1/10; //?? CALCULAR SEGUN ENTRADA??
	outVLowCCtx->width = (inVWidth/6)*2;	//MULTIPLOS DE 2??????
	outVLowCCtx->height = (inVHeight/6)*2; //????????????????
	outVLowCCtx->time_base = (AVRational){1, 25};
	outVLowCCtx->framerate = (AVRational){25, 1};
	outVLowCCtx->gop_size = 10;
	outVLowCCtx->max_b_frames = 1;
	outVLowCCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	outVLowCCtx->profile=FF_PROFILE_H264_HIGH;
	uint8_t *extradatalow=av_malloc(sizeof(uint8_t)*4);//Escribir extra_data para que el muxer escriba correctamente los archivos stream.mpd y init-stream0.mp4
	extradatalow[0]=1;extradatalow[1]=77;extradatalow[2]=64;extradatalow[3]=21;  //liberado por free_codec_context()
	outVLowCCtx->extradata=extradatalow; //(77,64,21 = (avc1.4D4015 = avc1 profile baseline preset 2.1)
	outVLowCCtx->extradata_size=4;
	response=av_opt_set(outVLowCCtx->priv_data,"preset","slow",0);
	if(response<0){
		logging("Cannot set codec option");
		return -1;
	}
	response=av_opt_set(outVLowCCtx->priv_data,"crf","44",0);
	if(response<0){
		logging("Cannot set codec option");
		return -1;
	}
	response=avcodec_parameters_from_context(outVLowStream->codecpar,outVLowCCtx);
	if(response<0){
		logging("Cannot copy params fro context");
		return -1;
	}
	outVLowStream->avg_frame_rate=(AVRational){25,1};


	//*********************************** PERFIL MEDIO **************************************************************************************************
	outVMidStream = avformat_new_stream(outFCtx, videoEnc);
	if (!outVMidStream) {
		logging("Failed allocating video mid output stream.");
		return AVERROR_UNKNOWN;
	}
	outVMidCCtx = avcodec_alloc_context3(videoEnc);
	if(outVMidCCtx<0){
		logging("Failed allocating video mid output codec context");
		return -1;
	}
	outVMidCCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	outVMidCCtx->bit_rate = inVBitrate*1/6; //?? CALCULAR SEGUN ENTRADA??
	outVMidCCtx->width = (inVWidth/3)*2;	//MULTIPLOS DE 2??????
	outVMidCCtx->height = (inVHeight/3)*2; //????????????????
	outVMidCCtx->time_base = (AVRational){1, 25};
	outVMidCCtx->framerate = (AVRational){25, 1};
	outVMidCCtx->gop_size = 10;
	outVMidCCtx->max_b_frames = 1;
	outVMidCCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	outVMidCCtx->extradata_size=4;
	outVMidCCtx->profile=FF_PROFILE_H264_MAIN;
	uint8_t *extradatamid=av_malloc(sizeof(uint8_t)*4);//Escribir extra_data para que el muxer escriba correctamente los archivos stream.mpd y init-stream0.mp4
	extradatamid[0]=1;extradatamid[1]=77;extradatamid[2]=64;extradatamid[3]=30;
	outVMidCCtx->extradata=extradatamid; //(77,64,20 = (avc1.4D401E = avc1 profile baseline preset 3.0)
	outVMidCCtx->extradata_size=4;
	response=av_opt_set(outVMidCCtx->priv_data,"crf","44",0);
	if(response<0){
		logging("Cannot set codec option");
		return -1;
	}
	avcodec_parameters_from_context(outVMidStream->codecpar,outVMidCCtx);
	outVMidStream->avg_frame_rate=(AVRational){25,1};


	//((******************************PERFIL ALTO ****************************************************************************************))
	outVHighStream = avformat_new_stream(outFCtx, videoEnc);
	if (!outVHighStream) {
		logging("Failed allocating video high output stream.");
		return AVERROR_UNKNOWN;
	}
	outVHighCCtx = avcodec_alloc_context3(videoEnc);
	if(outVHighCCtx<0){
		logging("Failed allocating video High output codec context");
		return -1;
	}
	outVHighCCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	outVHighCCtx->bit_rate = inVBitrate/10; //?? CALCULAR SEGUN ENTRADA??
	outVHighCCtx->width = inVWidth;	//MULTIPLOS DE 2??????
	outVHighCCtx->height = inVHeight; //????????????????
	outVHighCCtx->time_base = (AVRational){1, 25};
	outVHighCCtx->framerate = (AVRational){25, 1};
	outVHighCCtx->gop_size = 10;
	outVHighCCtx->max_b_frames = 1;
	outVHighCCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	outVHighCCtx->profile=FF_PROFILE_H264_MAIN;
	uint8_t *extradatahigh=av_malloc(sizeof(uint8_t)*4);//Escribir extra_data para que el muxer escriba correctamente los archivos stream.mpd y init-stream0.mp4
	extradatahigh[0]=1;extradatahigh[1]=77;extradatahigh[2]=40;extradatahigh[3]=31;
	outVHighCCtx->extradata_size=4;
	outVHighCCtx->extradata=extradatahigh;
	response=av_opt_set(outVHighCCtx->priv_data,"preset","slow",0);
	if(response<0){
		logging("Cannot set codec option");
		return -1;
	}
	char bufsize[9];
	sprintf(bufsize,"%d",inVBitrate*2);
	/*const AVOption *opt = av_opt_find(outVHighCCtx,"bufsize","",0,AV_OPT_SEARCH_CHILDREN);
	if(opt==NULL){
		logging("Cannot find codec option: bufsize(HIGH): option not found");
				return -1;
	}*/

	response=av_opt_set(outVHighCCtx, "bufsize",bufsize,AV_OPT_SEARCH_CHILDREN);
	if(response==AVERROR_OPTION_NOT_FOUND){
		logging("Cannot set codec option: bufsize(HIGH): option not found");
		return -1;
	}else if(response==AVERROR(ERANGE)){
		logging("Cannot set codec option: bufsize(HIGH): value out of range");
		return -1;
	}else if(response==AVERROR(EINVAL)){
		logging("Cannot set codec option: bufsize(HIGH) invalid value");
		return -1;
	}
	char maxrate[9];
	sprintf(maxrate,"%d",inVBitrate);
	response=av_opt_set(outVHighCCtx, "maxrate",maxrate,0);
	if(response<0){
		logging("Cannot set codec option:maxrate(HIGH)");
		return -1;
	}
	/*
	response=av_opt_set(outVHighCCtx,"crf","44",0);
		if(response<0){
			logging("Cannot set codec option");
			return -1;
		}*/   //       ALGUNAS OPCIONES SE ACCEDE POR PPRIV_DATA Y OTRAS POR EL OBJETO: NO ESTA DOCUMENTADO
	response=av_opt_set(outVHighCCtx->priv_data,"crf","44",0);
	if(response<0){
		logging("Cannot set codec option");
		return -1;
	}
	//response=av_opt_show2(outVHighCCtx,outVHighCCtx,AV_OPT_FLAG_ENCODING_PARAM && AV_OPT_FLAG_VIDEO_PARAM,0);	/* POPRQUE PPPRIVATE DATA???*/
	response=av_opt_show2(outVHighCCtx->priv_data,outVHighCCtx,AV_OPT_FLAG_ENCODING_PARAM && AV_OPT_FLAG_VIDEO_PARAM,0);	/* POPRQUE PPPRIVATE DATA???*/
	avcodec_parameters_from_context(outVHighStream->codecpar,outVHighCCtx);
	outVHighStream->avg_frame_rate=(AVRational){25,1};

	//ABRIR CODECS*******************************************ABRIR COEDCS***************************************************
	response=avcodec_open2(outACCtx,audioEnc,NULL);
	if(response<0){
		logging("imposible abrir codec de salida de audio (aac) : %s",av_err2str(response));
	}
	response=avcodec_open2(outVLowCCtx,videoEnc,NULL);
	if(response<0){
		logging("imposible abrir codec de salida de video calidad baja (h264) : %s",av_err2str(response));
	}
	response=avcodec_open2(outVMidCCtx,videoEnc,NULL);
	if(response<0){
		logging("imposible abrir codec de salida de video calidad media (h264) : %s",av_err2str(response));
	}
	response=avcodec_open2(outVHighCCtx,videoEnc,NULL);
	if(response<0){
		logging("imposible abrir codec de salida de video calidad alta (h264) : %s",av_err2str(response));
	}
	response=avcodec_open2(inVCCtx,decoV,NULL);
	if(response<0){
		logging("imposible abrir codec de entrada de video: %s",av_err2str(response));
	}

	//**************************************OPCIONES DEL MUXER *************************************************************************


	response=av_opt_set(outFCtx->priv_data,"adaptation_sets","id=0,streams=v id=1,streams=a",0); //Correspondencia de los streams a los adaptation sets (0 video, 1 audio)
	if(response<0)logging("error %s adaptation sets",av_err2str(response));
	response=av_opt_set(outFCtx->priv_data,"seg_duration","1",0);  //DURACION DE SEGMENTO
	if(response<0)logging("error %s seg duration",av_err2str(response));
	/*response=av_opt_set(outFCtx->priv_data,"use_template","1",0);
	if(response<0)logging("error %s use template",av_err2str(response));
	response=av_opt_set(outFCtx->priv_data,"use_timeline","1",0);
	if(response<0)logging("error %s use timeline",av_err2str(response));*/

	//*************************************************************ESCRIBIR HEADER *******************************************************************************
	response = avformat_write_header(outFCtx,NULL);
	if (response < 0) {
		logging("Error while writing file header: %s", av_err2str(response));
		return response;
	}
	//Reservando memoria para frame y  packet
	inFrame = av_frame_alloc();
	if (!inFrame)
	{
		logging("failed to allocated memory for AVFrame");
		return -1;
	}
	outFrame = av_frame_alloc();
	if (!outFrame)
	{
		logging("failed to allocated memory for AVFrame");
		return -1;
	}
	//Reservando memoria para packet

	inPacket = av_packet_alloc();
	if (!inPacket)
	{
		logging("failed to allocated memory for AVPacket");
		return -1;
	}


	outPacket = av_packet_alloc();
	if (!outPacket)
	{
		logging("failed to allocated memory for AVPacket");
		return -1;
	}
	// Contexto del Escalador ( de escala no de escalar el everest)
	sclMidCtx =sws_getContext(inVWidth,	inVHeight,inVCCtx->pix_fmt,(inVWidth/3)*2,(inVHeight/3)*2,	AV_PIX_FMT_YUV420P,	 SWS_BILINEAR, NULL, NULL, NULL);
	if (!sclMidCtx) {
		logging("Impossible to create scale context for the mid scaling ");
	}
	sclLowCtx =sws_getContext(inVWidth,	inVHeight,inVCCtx->pix_fmt,(inVWidth/6)*2,(inVHeight/6)*2,	AV_PIX_FMT_YUV420P,	 SWS_BILINEAR, NULL, NULL, NULL);
	if (!sclMidCtx) {
		logging("Impossible to create scale context for the low scaling ");
	}



	//***********************************************************************************************************************************
	//***************************************************** L O O P **********************************************************************
	// **********************************************************************************************************************************
	while (1)
	{
		AVCodecContext *inCC, *outCC,*aOutCC[3];
		AVStream *inStream,*outStream,*aOutStream[3];
		int isvideo=0;

		//*********************************LEER PAQUETE DE LA ENTRADA*****************************************
		response = av_read_frame(inFCtx, inPacket);
		if (response==AVERROR_EOF){
			break;
		} else if(response<0){
			logging("AV error cannot read frame :%s ",av_err2str(response));
			return response;
		}  else {

			//SI ES AUDIO ASIGNAR EL CODEC DE ENTRADA Y DE SALIDA Y LOS STREAMS DE ENTRADA Y SALIDA
			if(inFCtx->streams[inPacket->stream_index]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO){
				inCC=inACCtx;
				outCC=outACCtx;
				isvideo=0;
				inStream=inAStream;
				outStream=outAStream;
				//SI ES VIDEO ASIGNAR ENTRADAS Y ARRAYS DE SALIDAS (CODECS Y STREAMS)
			}else if(inFCtx->streams[inPacket->stream_index]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
				inCC=inVCCtx;
				aOutCC[0]=outVLowCCtx;
				aOutCC[1]=outVMidCCtx;
				aOutCC[2]=outVHighCCtx;
				isvideo=1;
				inStream=inVStream;
				aOutStream[0]=outVLowStream;
				aOutStream[1]=outVMidStream;
				aOutStream[2]=outVHighStream;
			}else{
				continue;
			}
			//*************************************ENVIA PAQUETE (DECODE)  *******************************************
			int response = avcodec_send_packet(inCC, inPacket);
			if (response == AVERROR(EINVAL)) {
				logging("Error while sending a packet to the decoder: invalid argument: %s", av_err2str(response));
				return response;
			}else if (response == AVERROR(EAGAIN)){
				logging("Error while sending a packet to the decoder: waiting to recive %s", av_err2str(response));
			}else if (response<0){
				logging("Error while sending a packet to the decoder: %s", av_err2str(response));
				return response;
			}
			//************************************RECIBE FRAME (DECODE) ***********************************************
			response = avcodec_receive_frame(inCC, inFrame);
			if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
				logging("Need more packets %s", av_err2str(response));
				continue;
			} else if (response < 0) {
				logging("Error while receiving a frame from the decoder: %s", av_err2str(response));
				return response;
			}
			//*********************************MULTIPLICANDO FRAME**********************************************************
			int times;
			//Si es video hace 3 pasadas del bucle for
			//si es audio hace solo una
			if(isvideo){
				times=3;
			}else {
				times=1;
			}
			for(int i=0;i<times;i++){ //1 vez para audio o 3 para video
				//************************************ESCALA EL FRAME (SCALE) *****************************************************
				uint8_t *src_data[4], *dst_data[4];
				int src_linesize[4],dst_linesize[4];
				int dst_bufsize;
				//outFrame=av_frame_alloc();
				if(outFrame==NULL){
					logging("No se ha podido reservar espacio para el frame de salida");
					return -1;
				}
				if(isvideo){
					if(i==0){//Low
						int width=(inVWidth/6)*2;
						int height = (inVHeight/6)*2;
						outFrame->width=width;
						outFrame->height=height;
						outFrame->format=AV_PIX_FMT_YUV420P;
						response=av_frame_get_buffer(outFrame,0);
						if(response<0){
							logging("imposible reservar buferes para el frame de salida, %s",av_err2str(response));
							return response;
						}
						for(int i=0;i<4;i++){
							dst_data[i]=outFrame->data[i];
							dst_linesize[i]=outFrame->linesize[i];
							src_data[i]=inFrame->data[i];
							src_linesize[i]=inFrame->linesize[i];
						}
						response=sws_scale(sclLowCtx,(const uint8_t * const*)src_data,src_linesize,0,inVHeight,dst_data,dst_linesize);
						if(response<0){
							logging("imposible escalar el frame ");
							return response;
						}
						outFrame->pts=inFrame->pts;
					}else if(i==1){
						int width=(inVWidth/3)*2;
						int height = (inVHeight/3)*2;
						dst_bufsize = response;
						outFrame->width=width;
						outFrame->height=height;
						outFrame->format=AV_PIX_FMT_YUV420P;
						response=av_frame_get_buffer(outFrame,0);
						if(response<0){
							logging("imposible reservar buferes para el frame de salida, %s",av_err2str(response));
							return response;
						}
						for(int i=0;i<4;i++){
							dst_data[i]=outFrame->data[i];
							dst_linesize[i]=outFrame->linesize[i];
							src_data[i]=inFrame->data[i];
							src_linesize[i]=inFrame->linesize[i];
						}
						response=sws_scale(sclMidCtx,(const uint8_t * const*)src_data,src_linesize,0,inVHeight,dst_data,dst_linesize);
						if(response<0){
													logging("imposible escalar el frame ");
													return response;
												}
						outFrame->pts=inFrame->pts;
					}else{
						response=av_frame_ref(outFrame,inFrame);
						if(response<0){
							logging("imposible copiar frame e entrada a salida");
							return response;
						}
					}
					outFrame->pict_type=AV_PICTURE_TYPE_NONE;//el encoder elige
					outStream=aOutStream[i];
				}else{	//AUDIO
					response=av_frame_ref(outFrame,inFrame);
					if(response<0){
						logging("imposible copiar frame e entrada a salida");
						return response;
					}
					outFrame->nb_samples= outACCtx->frame_size;
					outFrame->format= outACCtx->sample_fmt;
					outFrame->channel_layout=outACCtx->channel_layout;
				}
				//****************************************ENVIA FRAME ( ENNCODE ) ******************************************************
				response = avcodec_send_frame(isvideo?aOutCC[i]:outCC, outFrame);
				if (response == AVERROR(EAGAIN) || response == AVERROR_EOF ) {
					logging("Error necesitas mas input: %s", av_err2str(response)); //CREO QUE ESO ON PUEDE PASAR
				}else if(response==AVERROR(EINVAL)){
					logging("Error del codec????: %s", av_err2str(response)); //codec not opened, refcounted_frames not set, it is a decoder, or requires flush
					return response;
				}else if(response < 0){
					logging("Error while sending a frame to the encoder: %s", av_err2str(response));
					return response;
				}
				av_frame_unref(outFrame);

				while(response>=0){
					//******************************************RECIBE PAQUETE (ENCODEC)**************************************************************
					response = avcodec_receive_packet(isvideo?aOutCC[i]:outCC, outPacket);
					if (response == AVERROR(EAGAIN)) {
						break;
					}else if (response == AVERROR_EOF){
						logging("FIN!!!!!!: %s", av_err2str(response));
						break;
					}
					else if (response < 0) {
						logging("Error while receiving a packet from the enncoder: %s", av_err2str(response));
						return response;
					}
					if(isvideo){
						outPacket->stream_index=i+1;
					}else{
						outPacket->stream_index=0;
					}
					//escalado temporal (dts,pts)
					if(isvideo){
						outPacket->pts = av_rescale_q_rnd(outPacket->pts, inStream->time_base, outStream->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
						outPacket->dts = av_rescale_q_rnd(outPacket->dts, inStream->time_base, outStream->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
						outPacket->duration = av_rescale_q(outPacket->duration, inStream->time_base, outStream->time_base);
					}
					//********************************************ESCRIBIR PAQUETE (MUX) ********************************************************************
					response=av_interleaved_write_frame(outFCtx, outPacket);
					if (response==AVERROR(EAGAIN)) {
						logging( "Error muxing packet: more data needed\n");
						break;
					}else if (response==AVERROR_EOF) {
						logging( "End\n");
						break;
					}else if(response < 0){
						logging("Error muxing packet: %s",av_err2str(response));
						break;
					}
				}
			}
		}
		frame_index++;
		logging("forograma %d",frame_index);
		av_packet_unref(inPacket);


	}
	av_dump_format(inFCtx, 0, inputFileName, 0);
	av_dump_format(outFCtx, 0, out_file, 1);

	//**************************************************ESCRIBIR FINAL DEL ARCHIVO ***************************************************************
	response = av_write_trailer(outFCtx);
	if (response < 0) {
		logging("Error while writing the trailer in the file: %s", av_err2str(response));
		return response;
	}
	//***************************************************LIMPIAR************************************************************************************

	av_packet_unref(outPacket);
	av_packet_unref(inPacket);
	av_frame_unref(inFrame);
	av_frame_unref(outFrame);
	av_packet_free(&inPacket);
	av_packet_free(&outPacket);
	av_frame_free(&inFrame);
	av_frame_free(&outFrame);


	avcodec_free_context(&inACCtx);
	avcodec_free_context(&inVCCtx);
	avformat_close_input(&inFCtx);
	avformat_free_context(inFCtx);

	avcodec_free_context(&outVLowCCtx);
	avcodec_free_context(&outVHighCCtx);
	avcodec_free_context(&outVMidCCtx);
	avcodec_free_context(&outACCtx);

	avformat_free_context(outFCtx);

	return 0;
}

static void logging(const char *fmt, ...)
{
	va_list args;
	fprintf( stderr, "LOG: " );
	va_start( args, fmt );
	vfprintf( stderr, fmt, args );
	va_end( args );
	fprintf( stderr, "\n" );
}
