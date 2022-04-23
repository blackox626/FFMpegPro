//
// Created by blackox626 on 2022/4/23.
//

#ifndef FFMPEGPRO_AUDIOENCODER_H
#define FFMPEGPRO_AUDIOENCODER_H


class AudioEncoder {
public:
    AudioEncoder();

    virtual ~AudioEncoder();

    void encode_pcm_to_aac(const char *pcm_path, const char *aac_path);
};


#endif //FFMPEGPRO_AUDIOENCODER_H
