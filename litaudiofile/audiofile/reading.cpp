//
// Created by egordm on 11-11-2018.
//
// Warning this is my skeleton closed. Proceed cautiously

#include <structures/audio_container.h>
#include <structures/audio_buffer_interface_interleaved.h>
#include "reading.h"

using namespace litaudiofile;
using namespace litaudio;
using namespace litaudio::structures;

bool litaudiofile::AudioReader::read() {
    if (!open_file()) return false;

    if (!read_file()) return false;

    dst->copyFormat(tmp);

    processing::AudioConverter converter(tmp, dst);
    return converter.convert();
}

bool AudioReader::open_file() {
    format_context = avformat_alloc_context();

    // Open file
    if ((error = avformat_open_input(&format_context, path.c_str(), nullptr, nullptr)) < 0) {
        ffmpeg_utils::log_error(AudioReader_TAG, "Error opening the file.", error);
        return false;
    }

    // Get file format info
    if ((error = avformat_find_stream_info(format_context, nullptr)) < 0) {
        ffmpeg_utils::log_error(AudioReader_TAG, "Error finding the stream info.", error);
        return false;
    }

    // Get audio stream
    for (int i = 0; i < format_context->nb_streams; ++i) {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            stream = format_context->streams[i];
            break;
        }
    }

    if (stream == nullptr) {
        ffmpeg_utils::log_error(AudioReader_TAG, "Could not find any audio stream in the file.");
        return false;
    }

    // Find the right codec
    codec_params = stream->codecpar;
    codec = avcodec_find_decoder(codec_params->codec_id);

    if (codec == nullptr) {
        ffmpeg_utils::log_error(AudioReader_TAG, "Couldn't find a proper decoder.");
        return false;
    }

    if (codec == nullptr) {
        avformat_close_input(&format_context);
        ffmpeg_utils::log_error(AudioReader_TAG, "Couldn't find a proper decoder.");
        return false;
    }

    // Open the codec context with decoder
    codec_context = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_context, codec_params); // can error

    // Set correct reading format
    auto tmp_format = av_get_packed_sample_fmt(codec->sample_fmts[0]);
    if (ffmpeg_utils::supports_sample_fmt(codec, dst->getFormat())) tmp_format = dst->getFormat();
    codec_context->request_sample_fmt = tmp_format;

    // Create the temporary audio container
    reading_planar = static_cast<bool>(av_sample_fmt_is_planar(tmp_format));
    if (reading_planar) {
        tmp = new AudioContainerDeinterleaved<uint8_t>(codec_context->channels, codec_context->sample_rate);
        tmp_buffer_deinterleaved = dynamic_cast<AudioBufferDeinterleaved<uint8_t> *>(tmp->getBuffer());
    } else {
        tmp = new AudioContainerInterleaved<uint8_t>(codec_context->channels, codec_context->sample_rate);
        tmp_buffer_interleaved = reinterpret_cast<AudioBufferInterleaved<uint8_t> *>(tmp->getBuffer());
    }
    tmp->setFormat(tmp_format);

    if ((error = avcodec_open2(codec_context, codec, nullptr)) < 0) {
        ffmpeg_utils::log_error(AudioReader_TAG, "Couldn't open the context with the decoder.", error);
        return false;
    }

    int estimated_sample_count = static_cast<int>(format_context->duration / (float) AV_TIME_BASE *
                                                  codec_context->sample_rate);
    tmp->setSampleCount(estimated_sample_count);

    return true;
}

bool AudioReader::read_file() {
    // Allocate frame
    frame = av_frame_alloc();
    av_init_packet(&packet);

    // Read the data
    bool finished = false, success = false;
    int sample_count = 0;
    while (!finished) {
        success = read_frame(finished, sample_count);
        av_packet_unref(&packet);

        if (!success) return false;
    }

    // Set a definite sample count
    tmp->setSampleCount(sample_count);
    return true;
}

bool AudioReader::read_frame(bool &finished, int &sample_count) {
    if ((error = av_read_frame(format_context, &packet)) < 0) {
        if (error == AVERROR_EOF) {
            finished = true;
        } else {
            ffmpeg_utils::log_error(AudioReader_TAG, "Error reading frame.", error);
            return false;
        }
    }

    // Take data only form desired stream
    if (packet.stream_index != stream->id) {
        av_packet_unref(&packet);
        return true;
    }

    // Send data to decoder
    if ((error = avcodec_send_packet(codec_context, &packet)) < 0) {
        ffmpeg_utils::log_error(AudioReader_TAG, "Could not send packet for decoding ", error);
        av_packet_unref(&packet);
        return false;
    }

    // Recieve decoded data
    while ((error = avcodec_receive_frame(codec_context, frame)) == 0) {
        // Resize buffer if it is too small
        if (sample_count + frame->nb_samples > tmp->getSampleCount()) {
            tmp->setSampleCount(sample_count + frame->nb_samples * 2);
        }

        if (reading_planar) handle_frame_planar();
        else handle_frame_packed();
        sample_count += frame->nb_samples;
    }

    if (error < 0) {
        if (error == AVERROR_EOF) {
            error = 0;
        } else if (error != AVERROR(EAGAIN)) {
            ffmpeg_utils::log_error(AudioReader_TAG, "Could not decode frame ", error);
            return false;
        }
    }

    return true;
}

bool AudioReader::handle_frame_packed() {
    frame_size_bytes = frame->nb_samples * tmp->getChannelCount() * tmp->getSampleSize();
    memcpy(tmp_buffer_interleaved->getData() + write_cursor, frame->extended_data[0], frame_size_bytes);
    write_cursor += frame_size_bytes;
    return true;
}

bool AudioReader::handle_frame_planar() {
    frame_size_bytes = frame->nb_samples * tmp->getSampleSize();
    for (int c = 0; c < tmp->getChannelCount(); ++c) {
        memcpy(tmp_buffer_deinterleaved->getChannelPtr(c) + write_cursor, frame->extended_data[c], frame_size_bytes);
    }
    write_cursor += frame_size_bytes;
    return true;
}