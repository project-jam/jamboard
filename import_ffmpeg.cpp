#include "import_ffmpeg.h"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>
#include <windows.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

static FILE* fopen_utf8(const char* path, const char* mode) {
    wchar_t wpath[MAX_PATH], wmode[16];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) == 0) return fopen(path, mode);
    if (MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 16) == 0) return fopen(path, mode);
    return _wfopen(wpath, wmode);
}

static bool write_wav(const char* path, const uint8_t* data, size_t data_size, int sample_rate, int channels, int bits_per_sample)
{
    FILE* f = fopen_utf8(path, "wb");
    if (!f) return false;

    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;
    uint16_t num_channels = (uint16_t)channels;
    uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    uint16_t block_align = (uint16_t)(channels * bits_per_sample / 8);
    uint16_t bits = (uint16_t)bits_per_sample;
    uint32_t file_size = 36 + (uint32_t)data_size;

    fwrite("RIFF", 1, 4, f);
    fwrite(&file_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&num_channels, 2, 1, f);
    fwrite((const void*)&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    uint32_t ds = (uint32_t)data_size;
    fwrite(&ds, 4, 1, f);
    fwrite(data, 1, data_size, f);

    fclose(f);
    return true;
}

static bool write_ppm(const char* path, int w, int h, const uint8_t* rgb)
{
    FILE* f = fopen_utf8(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(rgb, 1, w * h * 3, f);
    fclose(f);
    return true;
}

bool ffmpeg_convert_audio(const std::string& src, const std::string& dst)
{
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, src.c_str(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    int stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_idx < 0) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVStream* stream = fmt_ctx->streams[stream_idx];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVCodecContext* dec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(dec_ctx, stream->codecpar);
    if (avcodec_open2(dec_ctx, decoder, nullptr) < 0) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVChannelLayout in_layout = dec_ctx->ch_layout;
    AVSampleFormat in_fmt = dec_ctx->sample_fmt;
    int in_rate = dec_ctx->sample_rate;

    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, 2);
    int out_rate = 44100;
    AVSampleFormat out_fmt = AV_SAMPLE_FMT_S16;

    SwrContext* swr = swr_alloc();
    if (!swr) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return false;
    }

    av_opt_set_chlayout(swr, "in_chlayout", &in_layout, 0);
    av_opt_set_int(swr, "in_sample_rate", in_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", in_fmt, 0);
    av_opt_set_chlayout(swr, "out_chlayout", &out_layout, 0);
    av_opt_set_int(swr, "out_sample_rate", out_rate, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", out_fmt, 0);

    if (swr_init(swr) < 0) {
        swr_free(&swr);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return false;
    }

    std::vector<uint8_t> all_pcm;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index != stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

        if (avcodec_send_packet(dec_ctx, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }

        while (avcodec_receive_frame(dec_ctx, frame) >= 0) {
            uint8_t* out_buf[8] = { nullptr };
            int out_linesize[8] = { 0 };
            int out_samples = swr_get_out_samples(swr, frame->nb_samples);
            av_samples_alloc(out_buf, out_linesize, 2, out_samples, out_fmt, 0);

            int converted = swr_convert(swr, out_buf, out_samples,
                (const uint8_t**)frame->data, frame->nb_samples);
            if (converted > 0) {
                int bytes = converted * 2 * av_get_bytes_per_sample(out_fmt);
                size_t old = all_pcm.size();
                all_pcm.resize(old + bytes);
                memcpy(all_pcm.data() + old, out_buf[0], bytes);
            }
            av_freep(&out_buf[0]);
            av_frame_unref(frame);
        }
        av_packet_unref(pkt);
    }

    avcodec_send_packet(dec_ctx, nullptr);
    while (avcodec_receive_frame(dec_ctx, frame) >= 0) {
        uint8_t* out_buf[8] = { nullptr };
        int out_linesize[8] = { 0 };
        int out_samples = swr_get_out_samples(swr, frame->nb_samples);
        av_samples_alloc(out_buf, out_linesize, 2, out_samples, out_fmt, 0);

        int converted = swr_convert(swr, out_buf, out_samples,
            (const uint8_t**)frame->data, frame->nb_samples);
        if (converted > 0) {
            int bytes = converted * 2 * av_get_bytes_per_sample(out_fmt);
            size_t old = all_pcm.size();
            all_pcm.resize(old + bytes);
            memcpy(all_pcm.data() + old, out_buf[0], bytes);
        }
        av_freep(&out_buf[0]);
        av_frame_unref(frame);
    }

    bool ok = write_wav(dst.c_str(), all_pcm.data(), all_pcm.size(), out_rate, 2, 16);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);

    return ok;
}

bool ffmpeg_extract_thumbnail(const std::string& src, const std::string& dst)
{
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, src.c_str(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    int stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_idx < 0) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVStream* stream = fmt_ctx->streams[stream_idx];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVCodecContext* dec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(dec_ctx, stream->codecpar);
    if (avcodec_open2(dec_ctx, decoder, nullptr) < 0) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool got_frame = false;

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index != stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

        if (avcodec_send_packet(dec_ctx, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }

        if (avcodec_receive_frame(dec_ctx, frame) >= 0) {
            got_frame = true;
            av_packet_unref(pkt);
            break;
        }
        av_packet_unref(pkt);
    }

    if (!got_frame) {
        avcodec_send_packet(dec_ctx, nullptr);
        if (avcodec_receive_frame(dec_ctx, frame) >= 0)
            got_frame = true;
    }

    bool success = false;
    if (got_frame) {
        int w = frame->width;
        int h = frame->height;

        SwsContext* sws = sws_getContext(w, h, dec_ctx->pix_fmt,
            w, h, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (sws) {
            std::vector<uint8_t> rgb(w * h * 3);
            uint8_t* dest[4] = { rgb.data(), nullptr, nullptr, nullptr };
            int dest_stride[4] = { w * 3, 0, 0, 0 };

            sws_scale(sws, frame->data, frame->linesize, 0, h, dest, dest_stride);
            success = write_ppm(dst.c_str(), w, h, rgb.data());
            sws_freeContext(sws);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);

    return success;
}
