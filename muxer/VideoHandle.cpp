//
// Created by blackox626 on 2022/4/26.
//

/**
 * 视频处理
 * 解码视频，然后转换成720x1080，然后编码成h264
 */

#ifndef TARGET_VIDEO_WIDTH
#define TARGET_VIDEO_WIDTH  720
#define TARGET_VIDEO_HEIGHT  1280
#endif

#include <iostream>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
};

class VideoHandle {

public:

    void handle_video(std::vector<char *> mp4_paths,
                      std::function<void(const AVCodecContext *, AVPacket *, bool)> callback) {
        // 视频编码器相关
        const AVCodec *video_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        video_encoder_context = avcodec_alloc_context3(video_codec);
        video_encoder_context->pix_fmt = AV_PIX_FMT_YUV420P;
        video_encoder_context->width = TARGET_VIDEO_WIDTH;
        video_encoder_context->height = TARGET_VIDEO_HEIGHT;
        video_encoder_context->bit_rate = 2000 * 1024;
        video_encoder_context->gop_size = 10;
        video_encoder_context->time_base = {1, 25};
        video_encoder_context->framerate = {25, 1};
        // b帧的数量
        video_encoder_context->max_b_frames = 1;
        // 设置H264的编码器参数为延迟模式，提高编码质量，但是会造成编码速度下降
//        av_opt_set(video_encoder_context->priv_data, "preset", "slow", 0);
        int ret = avcodec_open2(video_encoder_context, video_codec, nullptr);
        if (ret < 0) {
            std::cout << "视频编码器打开失败" << std::endl;
            return;
        }
        AVFormatContext *avFormatContext = nullptr;
        AVCodecContext *decoder_context = nullptr;
        AVPacket *avPacket = av_packet_alloc();
        AVFrame *avFrame = av_frame_alloc();
        std::vector<AVPacket *> pack_vector = std::vector<AVPacket *>();
        // 前面视频的pts累计
        int64_t previous_pts = 0;
        // 但前视频最后的pts
        int64_t last_pts = 0;
        while (!mp4_paths.empty()) {
            // 先释放旧的
            previous_pts += last_pts;
            avcodec_free_context(&decoder_context);
            avformat_free_context(avFormatContext);
            const char *mp4 = mp4_paths.at(0);
            mp4_paths.erase(mp4_paths.cbegin());
            avFormatContext = avformat_alloc_context();
            ret = avformat_open_input(&avFormatContext, mp4, nullptr, nullptr);
            if (ret < 0) {
                std::cout << "视频文件打开失败" << std::endl;
                break;
            }
            int video_index = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            if (video_index < 0) {
                std::cout << "没有找到视频流" << std::endl;
                break;
            }
            const AVCodec *avCodec = avcodec_find_decoder(avFormatContext->streams[video_index]->codecpar->codec_id);
            decoder_context = avcodec_alloc_context3(avCodec);
            avcodec_parameters_to_context(decoder_context, avFormatContext->streams[video_index]->codecpar);
            ret = avcodec_open2(decoder_context, avCodec, nullptr);
            if (ret < 0) {
                std::cout << "视频解码器打开失败" << std::endl;
                break;
            }

            while (true) {
                ret = av_read_frame(avFormatContext, avPacket);
                if (ret < 0) {
                    std::cout << "视频包读取完毕" << std::endl;
                    break;
                }
                if (avPacket->stream_index != video_index) {
                    av_packet_unref(avPacket);
                    continue;
                }
                ret = avcodec_send_packet(decoder_context, avPacket);
                if (ret < 0) {
                    char error[1024];
                    av_strerror(ret, error, 1024);
                    std::cout << "视频包发送解码失败" << error << std::endl;
                    break;
                }
                while (true) {
                    ret = avcodec_receive_frame(decoder_context, avFrame);
                    if (ret == AVERROR(EAGAIN)) {
                        std::cout << "视频包获取解码帧：EAGAIN" << std::endl;
                        break;
                    } else if (ret < 0) {
                        std::cout << "视频包获取解码帧：fail" << std::endl;
                    } else {
                        std::cout << "重新编码视频" << std::endl;
                        pack_vector.clear();
                        // 转换成统一的pts
                        last_pts = av_rescale_q(avFrame->pts, avFormatContext->streams[video_index]->time_base,
                                                AV_TIME_BASE_Q);
                        avFrame->pts = previous_pts + last_pts;
                        // 尺寸转换
                        scale_yuv(avFrame);
                        // 重新编码
                        encode_video(pack_vector, out_frame);
                        while (!pack_vector.empty()) {
                            AVPacket *packet = pack_vector.at(0);
                            // 回调
                            callback(video_encoder_context, packet, false);
                            pack_vector.erase(pack_vector.cbegin());
                        }
                    }
                }
                av_packet_unref(avPacket);
            }
        }
        avcodec_free_context(&decoder_context);
        avformat_free_context(avFormatContext);
        // 回调结束
        callback(video_encoder_context, nullptr, true);
    }

    void scale_yuv(AVFrame *frame) {
        swsContext = sws_getCachedContext(swsContext, frame->width, frame->height, AV_PIX_FMT_YUV420P,
                                          TARGET_VIDEO_WIDTH, TARGET_VIDEO_HEIGHT, AV_PIX_FMT_YUV420P,
                                          SWS_BILINEAR,
                                          nullptr, nullptr, nullptr);

        if (nullptr == out_frame) {
            out_frame = av_frame_alloc();
            out_frame->format = AV_PIX_FMT_YUV420P;
            out_frame->width = TARGET_VIDEO_WIDTH;
            out_frame->height = TARGET_VIDEO_HEIGHT;
            av_frame_get_buffer(out_frame, 0);
        }
        // 转换
        int ret = sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height, out_frame->data,
                            out_frame->linesize);
        // pts
        std::cout << "frame->pts:" << frame->pts << std::endl;
        out_frame->pts = frame->pts;
        if (ret < 0) {
            std::cout << "图像缩放失败" << std::endl;
            return;
        }
    }

    void encode_video(std::vector<AVPacket *> &pack_vector, AVFrame *frame) {
        int ret = avcodec_send_frame(video_encoder_context, frame);
        if (ret < 0) {
            std::cout << "视频发送编码失败" << std::endl;
            return;
        }
        while (true) {
            AVPacket *packet = av_packet_alloc();
            ret = avcodec_receive_packet(video_encoder_context, packet);
            if (ret == AVERROR(EAGAIN)) {
                std::cout << "视频编码：EAGAIN" << std::endl;
                break;
            } else if (ret < 0) {
                std::cout << "视频编码：fail" << std::endl;
                break;
            } else {
                pack_vector.push_back(packet);
            }
        }
    }

    // 视频编码器
    AVCodecContext *video_encoder_context = nullptr;

    // 视频转换专用
    SwsContext *swsContext = nullptr;
    AVFrame *out_frame = nullptr;

};