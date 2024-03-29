//
// Created by egordm on 7-1-19.
//

#pragma once

#include <utils/litaudio_utils.h>
#include "audio_buffer_interface.h"

namespace litaudio { namespace structures {
    class AudioBufferInterleavedBaseInterface : public AudioBufferInterface {
    public:
        using AudioBufferInterface::AudioBufferInterface;

        virtual uint8_t *getDataPtr() = 0;

        const uint8_t *getDataPtr() const {
            return const_cast<AudioBufferInterleavedBaseInterface *>(this)->getDataPtr();
        }

        std::vector<uint8_t *> getDataFull() override {
            return {getDataPtr()};
        }

        std::vector<const uint8_t *> getDataFullC() const override {
            return {getDataPtr()};
        }

        bool copyData(const AudioBufferInterface *srcr) override {
            auto src = dynamic_cast<const AudioBufferInterleavedBaseInterface *>(srcr);
            if (src == nullptr) return false; // Different buffer types unsupported
            memcpy(this->getDataPtr(), src->getDataPtr(),
                   (size_t) this->getSampleCount() * getChannelCount() * getSampleSize());
            return true;
        }
    };

    template<typename T>
    class AudioBufferInterleavedInterface : public AudioBufferInterleavedBaseInterface {
    public:
        AudioBufferInterleavedInterface(int channel_count, int capacity)
                : AudioBufferInterleavedBaseInterface(channel_count, capacity, utils::deduce_format<T>(false)) {}

        const T *getData() const {
            return const_cast<AudioBufferInterleavedInterface *>(this)->getData();
        }

        virtual T *getData() = 0;

        uint8_t *getDataPtr() override {
            return reinterpret_cast<uint8_t *>(getData());
        }
    };

    template<typename T>
    class AudioBufferInterleaved : public AudioBufferInterleavedInterface<T>, public AudioBufferModifiableInterface {
    protected:
        std::vector<T> buffer;

    public:
        AudioBufferInterleaved(int channel_count, int capacity)
                : AudioBufferInterleavedInterface<T>(channel_count, capacity) {
            int data_per_sample = this->getSampleSize() / sizeof(T);
            buffer.resize((unsigned long) std::max(0, data_per_sample * this->getSampleCount() * channel_count));
        }

        T *getData() override {
            return buffer.data();
        }

        void reset() override {
            memset(getData(), 0,
                   static_cast<size_t>(this->getSampleCount() * this->getChannelCount() * this->getSampleSize()));
        }

        void clear() override {
            buffer.clear();
        }

        void setChannelCount(int channel_count) override {
            AudioBufferInterface::channel_count = channel_count;
        }

        void setSampleCount(int sample_count) override {
            AudioBufferInterface::capacity = sample_count;
            int data_per_sample = this->getSampleSize() / sizeof(T);
            AudioBufferInterleaved<T>::buffer.resize(
                    static_cast<unsigned long>(this->getSampleCount() * this->getChannelCount() * data_per_sample));
        }
    };
}}
