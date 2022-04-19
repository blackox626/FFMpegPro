//
// Created by blackox626 on 2022/4/19.
//

#ifndef FFMPEGPRO_AUDIODECODER_H
#define FFMPEGPRO_AUDIODECODER_H


class AudioDecoder {

public:
    AudioDecoder();

    ~AudioDecoder();

    void decode_audio(std::string media_path, std::string pcm_path);
};


#endif //FFMPEGPRO_AUDIODECODER_H
