//
// Created by blackox626 on 2022/6/6.
//

#include "FFMpegSimplePlayer.h"

#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include "libavutil/imgutils.h"

#include <SDL2/SDL.h>
}

//Refresh Event
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)


#define SDL_AUDIO_BUFFER_SIZE 1024
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

//解码流数据到
int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size);

int thread_exit = 0;
int thread_pause = 0;

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;//首尾指针
    int nb_packets;                //包个数
    int size;                    //队列大小
    SDL_mutex *mutex;                //队列互斥锁
    SDL_cond *cond;
} PacketQueue;

int sfp_refresh_thread(void *opaque) {
    thread_exit = 0;
    thread_pause = 0;

    while (!thread_exit) {
        if (!thread_pause) {
            SDL_Event event;
            event.type = SFM_REFRESH_EVENT;
            SDL_PushEvent(&event);
        }
        SDL_Delay(40);
    }
    thread_exit = 0;
    thread_pause = 0;
    //Break
    SDL_Event event;
    event.type = SFM_BREAK_EVENT;
    SDL_PushEvent(&event);

    return 0;
}

void audio_callback(void *userdata, Uint8 *stream, int len) {

    SDL_memset(stream, 0, len);

    auto *aCodecCtx = (AVCodecContext *) userdata;
    int len1, audio_data_size;

    uint8_t audio_buf[8192];
    unsigned int audio_buf_size = 0;
    unsigned int audio_buf_index = 0;

    /*   len是由SDL传入的SDL缓冲区的大小，如果这个缓冲未满，我们就一直往里填充数据 */
    while (len > 0) {
        /*  audio_buf_index 和 audio_buf_size 标示我们自己用来放置解码出来的数据的缓冲区，*/
        /*   这些数据待copy到SDL缓冲区， 当audio_buf_index >= audio_buf_size的时候意味着我*/
        /*   们的缓冲为空，没有数据可供copy，这时候需要调用audio_decode_frame来解码出更
         /*   多的桢数据 */

        if (audio_buf_index >= audio_buf_size) {
            audio_data_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));
            /* audio_data_size < 0 标示没能解码出数据，我们默认播放静音 */
            if (audio_data_size < 0) {
                /* silence */
                audio_buf_size = 1024;
                /* 清零，静音 */
                memset(audio_buf, 0, audio_buf_size);
            } else {
                audio_buf_size = audio_data_size;
            }
            audio_buf_index = 0;
        }
        /*  查看stream可用空间，决定一次copy多少数据，剩下的下次继续copy */
        len1 = audio_buf_size - audio_buf_index;
        if (len1 > len) {
            len1 = len;
        }

//        memcpy(stream, (uint8_t *) audio_buf + audio_buf_index, len1);
        SDL_MixAudio(stream, audio_buf, len, SDL_MIX_MAXVOLUME);
        len -= len1;
//        stream += len1;
        audio_buf_index += len1;
    }
}

AVFormatContext *pFormatCtx;
int i, videoindex, audioindex;
AVCodecContext *pCodecCtx, *aCodecCtx;
const AVCodec *pCodec, *aCodec;
AVFrame *pFrame, *pFrameYUV, *aFrame;
unsigned char *out_buffer;
AVPacket *packet;
int ret, got_picture;

PacketQueue *audioq;

//------------SDL----------------
int screen_w, screen_h;
SDL_Window *screen;
SDL_Renderer *sdlRenderer;
SDL_Texture *sdlTexture;
SDL_Rect sdlRect;
SDL_Thread *video_tid;
SDL_Event event;

struct SwsContext *img_convert_ctx;

FILE *audio_pcm;

//初始化队列
void packet_queue_init(PacketQueue *q) {
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

//数据进入到队列中
int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
    AVPacketList *pkt1;
    if (!pkt) {
        return -1;
    }
    pkt1 = (AVPacketList *) av_malloc(sizeof(AVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    SDL_LockMutex(q->mutex);
    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
    return 0;
}

//从队列中取走数据
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
    AVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;) {

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

//解码流数据到
int audio_decode_frame(AVCodecContext *codecCtx, uint8_t *audio_buf, int buf_size) {
    AVPacket pkt;
    uint8_t *audio_pkt_data = NULL;
    int audio_pkt_size = 0;
    int len1, _data_size;

    for (;;) {
        if (packet_queue_get(audioq, &pkt, 1) < 0) //没有数据了，暂时认为播放完成了
        {
            return -1;
        }
        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
        while (audio_pkt_size > 0) {
            int ret = avcodec_send_packet(aCodecCtx, &pkt);

            if (ret == AVERROR(EAGAIN)) {
                std::cout << "发送解码EAGAIN：" << std::endl;
            } else if (ret < 0) {
                char error[1024];
                av_strerror(ret, error, 1024);
                std::cout << "发送解码失败：" << error << std::endl;
                return _data_size;
            }
            while (true) {
                ret = avcodec_receive_frame(aCodecCtx, aFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    std::cout << "音频解码失败：" << std::endl;
                    return _data_size;
                }
                // 每个采样数据量的大小
                int data_size = av_get_bytes_per_sample(aCodecCtx->sample_fmt);
                std::cout << "bytes_per_sample:" << data_size << std::endl;

                // 每帧采样数
                int nb_samples = aFrame->nb_samples;
                std::cout << "nb_samples:" << nb_samples << std::endl;
                /**
                 * P表示Planar（平面），其数据格式排列方式为 :
                   LLLLLLRRRRRRLLLLLLRRRRRRLLLLLLRRRRRRL...（每个LLLLLLRRRRRR为一个音频帧）
                   而不带P的数据格式（即交错排列）排列方式为：
                   LRLRLRLRLRLRLRLRLRLRLRLRLRLRLRLRLRLRL...（每个LR为一个音频样本）
                   播放范例：   ffplay -ar 44100 -ac 2 -f f32le pcm文件路径
                   并不是每一种都是这样的格式
                 */

                /**
                 * ffplay -ar 44100 -ac 2 -f f32le -i pcm文件路径
                    -ar 表示采样率
                    -ac 表示音频通道数
                    -f 表示 pcm 格式，sample_fmts + le(小端)或者 be(大端)
                    sample_fmts可以通过ffplay -sample_fmts来查询
                    -i 表示输入文件，这里就是 pcm 文件
                 *
                 */

                /**
                 * 大端模式，是指数据的高字节保存在内存的低地址中，而数据的低字节保存在内存的高地址中
                 * 小端模式，是指数据的高字节保存在内存的高地址中，而数据的低字节保存在内存的低地址中
                 */

                /**
                 * 需要注意的一点是planar仅仅是FFmpeg内部使用的储存模式，我们实际中所使用的音频都是packed模式的，
                 * 也就是说我们使用FFmpeg解码出音频PCM数据后，如果需要写入到输出文件，应该将其转为packed模式的输出。
                 */
                const char *fmt_name = av_get_sample_fmt_name(aCodecCtx->sample_fmt);
                AVSampleFormat pack_fmt = av_get_packed_sample_fmt(aCodecCtx->sample_fmt);
                std::cout << "fmt_name:" << fmt_name << std::endl;
                std::cout << "pack_fmt:" << pack_fmt << std::endl;
                std::cout << "frame->format:" << aFrame->format << std::endl;

//                float *sample_buffer = (float *) malloc(aFrame->nb_samples * 2 * 4);
//                memset(sample_buffer, 0, aFrame->nb_samples * 2 * 4);

                if (av_sample_fmt_is_planar(aCodecCtx->sample_fmt)) {
                    std::cout << "pcm planar模式" << std::endl;
                    for (int i = 0; i < aFrame->nb_samples; i++) {
                        for (int ch = 0; ch < aCodecCtx->channels; ch++) {
                            // 需要储存为pack模式
//                            fwrite(aFrame->data[ch] + data_size * i, 1, data_size, audio_pcm);

                            memcpy(audio_buf, aFrame->data[ch] + data_size * i, data_size);
                            audio_buf += data_size;
                        }
                    }
                } else {
                    std::cout << "pcm Pack模式" << std::endl;
//                    fwrite(frame->data[0], 1, frame->linesize[0], audio_pcm);
                }

//                memcpy(audio_buf, aFrame->data, aFrame->nb_samples * 2 * 4);
                _data_size = aFrame->nb_samples * 2 * 4;
                audio_buf -= _data_size;
//                fwrite(audio_buf, 1, _data_size, audio_pcm);

                return _data_size;
            }

//            int got_picture;
//
//            int ret = avcodec_decode_audio4(aCodecCtx, audioFrame, &got_picture, &pkt);
//            if (ret < 0) {
//                printf("Error in decoding audio frame.\n");
//                exit(0);
//            }
//
//            if (got_picture) {
//                int in_samples = audioFrame->nb_samples;
//                short *sample_buffer = (short *) malloc(audioFrame->nb_samples * 2 * 2);
//                memset(sample_buffer, 0, audioFrame->nb_samples * 4);
//
//                int i = 0;
//                float *inputChannel0 = (float *) (audioFrame->extended_data[0]);
//
//                // Mono  单声道
//                if (audioFrame->channels == 1) {
//                    for (i = 0; i < in_samples; i++) {
//                        float sample = *inputChannel0++;
//                        if (sample < -1.0f) {
//                            sample = -1.0f;
//                        } else if (sample > 1.0f) {
//                            sample = 1.0f;
//                        }
//
//                        sample_buffer[i] = (int16_t) (sample * 32767.0f);
//                    }
//                } else { // Stereo  双声道
//                    float *inputChannel1 = (float *) (audioFrame->extended_data[1]);
//                    for (i = 0; i < in_samples; i++) {
//                        sample_buffer[i * 2] = (int16_t) ((*inputChannel0++) * 32767.0f);
//                        sample_buffer[i * 2 + 1] = (int16_t) ((*inputChannel1++) * 32767.0f);
//                    }
//                }
////                fwrite(sample_buffer, 2, in_samples*2, pcmOutFp);
//                memcpy(audio_buf, sample_buffer, in_samples * 4);
//                free(sample_buffer);
//            }
//
//            audio_pkt_size -= ret;
//
//            if (audioFrame->nb_samples <= 0) {
//                continue;
//            }
//
//            data_size = audioFrame->nb_samples * 4;


        }
//        if (pkt.data)
//            av_free_packet(&pkt);
    }
}

FFMpegSimplePlayer::~FFMpegSimplePlayer() {
    sws_freeContext(img_convert_ctx);

    SDL_Quit();
    //--------------
    av_packet_free(&packet);
    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);
}

void FFMpegSimplePlayer::play(const char *filepath) {

//    audio_pcm = fopen("/Users/blackox626/CLionProjects/FFMpegPro/resource/audio2.pcm", "wb");

    //char filepath[]="bigbuckbunny_480x272.h265";
//        char filepath[]="Titanic.ts";

//        av_register_all();
//        avformat_network_init();
    pFormatCtx = avformat_alloc_context();

    if (avformat_open_input(&pFormatCtx, filepath, NULL, NULL) != 0) {
        printf("Couldn't open input stream.\n");
        return;
    }
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        printf("Couldn't find stream information.\n");
        return;
    }
    videoindex = -1;
    audioindex = -1;
    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoindex = i;
        }
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioindex = i;
        }
    }
    if (videoindex == -1 && audioindex == -1) {
        printf("Didn't find a video / audio stream.\n");
        return;
    }
//        pCodecCtx = pFormatCtx->streams[videoindex]->codec;

    pCodec = avcodec_find_decoder(pFormatCtx->streams[videoindex]->codecpar->codec_id);
    pCodecCtx = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoindex]->codecpar);

    if (pCodec == NULL) {
        printf("video Codec not found.\n");
        return;
    }
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        printf("Could not open video codec.\n");
        return;
    }

    aCodec = avcodec_find_decoder(pFormatCtx->streams[audioindex]->codecpar->codec_id);
    aCodecCtx = avcodec_alloc_context3(aCodec);
    avcodec_parameters_to_context(aCodecCtx, pFormatCtx->streams[audioindex]->codecpar);

    if (aCodec == NULL) {
        printf("audio Codec not found.\n");
        return;
    }
    if (avcodec_open2(aCodecCtx, aCodec, NULL) < 0) {
        printf("Could not open audio codec.\n");
        return;
    }

    pFrame = av_frame_alloc();
    pFrameYUV = av_frame_alloc();

    aFrame = av_frame_alloc();

    out_buffer = (unsigned char *) av_malloc(
            av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
                         AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);

    //Output Info-----------------------------
    printf("---------------- File Information ---------------\n");
    av_dump_format(pFormatCtx, 0, filepath, 0);
    printf("-------------------------------------------------\n");

    img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                                     pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL,
                                     NULL, NULL);


    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        printf("Could not initialize SDL - %s\n", SDL_GetError());
        return;
    }

    SDL_LockAudio();
    SDL_AudioSpec spec, wanted_spec;
    wanted_spec.freq = aCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_F32SYS;
    wanted_spec.channels = aCodecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = aCodecCtx->frame_size;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = aCodecCtx;

    if (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
        fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
        return;
    }

    SDL_UnlockAudio();
    SDL_PauseAudio(0);

    int out_buffer_size = av_samples_get_buffer_size(nullptr,aCodecCtx->channels,aCodecCtx->frame_size,AV_SAMPLE_FMT_FLTP,1);

    audioq = (PacketQueue *) malloc(sizeof(PacketQueue));
    packet_queue_init(audioq);


    //SDL 2.0 Support for multiple windows
    screen_w = pCodecCtx->width;
    screen_h = pCodecCtx->height;
    screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              screen_w, screen_h, SDL_WINDOW_OPENGL);

    if (!screen) {
        printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
        return;
    }
    sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
    //IYUV: Y + U + V  (3 planes)
    //YV12: Y + V + U  (3 planes)
    sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width,
                                   pCodecCtx->height);

    sdlRect.x = 0;
    sdlRect.y = 0;
    sdlRect.w = screen_w;
    sdlRect.h = screen_h;

//    packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    packet = av_packet_alloc();

    video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);
    //------------SDL End------------

    //Event Loop

    for (;;) {
        //Wait
        SDL_WaitEvent(&event);
        if (event.type == SFM_REFRESH_EVENT) {
            while (true) {
                if (av_read_frame(pFormatCtx, packet) < 0)
                    thread_exit = 1;

                if (packet->stream_index == videoindex || packet->stream_index == audioindex)
                    break;
            }
//                ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);

            if (packet->stream_index == videoindex) {
                ret = avcodec_send_packet(pCodecCtx, packet);
                if (ret < 0) {
                    std::cout << "视频发送解码失败:" << av_err2str(ret) << std::endl;
                }
                while (true) {
                    ret = avcodec_receive_frame(pCodecCtx, pFrame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        std::cout << "avcodec_receive_frame：" << av_err2str(ret) << std::endl;
                        break;
                    } else if (ret < 0) {
                        std::cout << "视频解码失败：" << std::endl;
                        return;
                    } else {
                        sws_scale(img_convert_ctx, (const unsigned char *const *) pFrame->data, pFrame->linesize, 0,
                                  pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
                        //SDL---------------------------
                        SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
                        SDL_RenderClear(sdlRenderer);
                        //SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );
                        SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
                        SDL_RenderPresent(sdlRenderer);
                        //SDL End-----------------------
                    }
                }
            } else if (packet->stream_index == audioindex) {
                packet_queue_put(audioq, packet);
            }


        } else if (event.type == SDL_KEYDOWN) {
            //Pause
            if (event.key.keysym.sym == SDLK_SPACE)
                thread_pause = !thread_pause;
        } else if (event.type == SDL_QUIT) {
            thread_exit = 1;
        } else if (event.type == SFM_BREAK_EVENT) {
            break;
        }

    }
    av_packet_unref(packet);
}


FFMpegSimplePlayer::FFMpegSimplePlayer() {}
