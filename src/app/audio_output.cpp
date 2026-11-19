#include "app/audio_output.hpp"

#include "core/dsp.hpp"

#include <windows.h>
#include <avrt.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

namespace esl {
namespace {

constexpr std::size_t blockFrames = 384; // 8 ms at 48 kHz.
constexpr std::size_t numBuffers = 4;

class EventHandle {
public:
    EventHandle() : handle(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}
    ~EventHandle() { if (handle) CloseHandle(handle); }
    EventHandle(const EventHandle&) = delete;
    EventHandle& operator=(const EventHandle&) = delete;
    const HANDLE handle;
};

struct DeviceBuffer {
    std::array<std::int16_t, blockFrames * 2> pcm{};
    WAVEHDR header{};
};

class WaveStream {
public:
    ~WaveStream() { close(); }
    bool active() const { return device_ != nullptr; }
    HANDLE event() const { return done_.handle; }
    unsigned open() {
        if (!done_.handle) return MMSYSERR_NOMEM;
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = liveSampleRate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = 4;
        format.nAvgBytesPerSec = liveSampleRate * format.nBlockAlign;
        const auto result = waveOutOpen(&device_, WAVE_MAPPER, &format,
                                        reinterpret_cast<DWORD_PTR>(done_.handle), 0, CALLBACK_EVENT);
        if (result != MMSYSERR_NOERROR) {
            device_ = nullptr;
            return result;
        }
        next_ = 0;
        for (auto& buffer : buffers_) {
            buffer.header = {};
            buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.pcm.data());
            buffer.header.dwBufferLength = sizeof(buffer.pcm);
            if (const auto err = waveOutPrepareHeader(device_, &buffer.header, sizeof(buffer.header)); err != MMSYSERR_NOERROR) {
                close();
                return err;
            }
        }
        ResetEvent(done_.handle);
        return MMSYSERR_NOERROR;
    }

    void close() {
        if (!device_) return;
        waveOutReset(device_);
        for (auto& buffer : buffers_) {
            if (buffer.header.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(device_, &buffer.header, sizeof(buffer.header));
        }
        waveOutClose(device_);
        device_ = nullptr;
    }

    bool hasRoom() const { return !(buffers_[next_].header.dwFlags & WHDR_INQUEUE); }

    bool empty() const {
        return std::none_of(buffers_.begin(), buffers_.end(), [](const DeviceBuffer& buffer) {
            return (buffer.header.dwFlags & WHDR_INQUEUE) != 0;
        });
    }

    unsigned write(const std::array<StereoFrame, blockFrames>& frames) {
        auto& buffer = buffers_[next_];
        auto toPcm = [](float sample) {
            return static_cast<std::int16_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
        };
        for (std::size_t i = 0; i < frames.size(); ++i) {
            buffer.pcm[2 * i] = toPcm(frames[i].left);
            buffer.pcm[2 * i + 1] = toPcm(frames[i].right);
        }
        const auto error = waveOutWrite(device_, &buffer.header, sizeof(buffer.header));
        next_ = (next_ + 1) % buffers_.size();
        return error;
    }

private:
    EventHandle done_;
    HWAVEOUT device_ = nullptr;
    std::array<DeviceBuffer, numBuffers> buffers_{};
    std::size_t next_ = 0;
};

} // namespace

struct AudioOutput::Worker {
    EventHandle wake;
    mutable std::mutex settingsMutex;
    mutable std::mutex meterMutex;
    Settings current;
    AudioSnapshot meter;
    std::atomic_bool quit{false};
    std::thread thread;

    Worker() {
        if (wake.handle) thread = std::thread([this] { run(); });
        else {
            meter.stream = StreamStatus::NoDevice;
            meter.deviceError = MMSYSERR_NOMEM;
        }
    }

    ~Worker() {
        quit.store(true, std::memory_order_relaxed);
        if (wake.handle) SetEvent(wake.handle);
        if (thread.joinable()) thread.join();
    }

    void run() {
        using Clock = std::chrono::steady_clock;
        using namespace std::chrono_literals;
        DWORD taskIndex = 0;
        const HANDLE priority = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        Synthesizer synth(liveSampleRate);
        WaveStream stream;
        std::array<StereoFrame, blockFrames> frames;
        std::array<float, scopeFrames> history{};
        std::size_t writePosition = 0;
        int blocks = 0;
        int tail = 0;
        double peak = 0.0;
        unsigned error = 0;
        unsigned underruns = 0;
        bool streaming = false;
        Program previousProgram = Program::Manual;
        std::uint32_t previousToken = 0;
        auto retry = Clock::time_point::min();
        auto fallback = Clock::now();

        auto publish = [&](StreamStatus status) {
            std::scoped_lock lock(meterMutex);
            meter.engine = synth.state();
            meter.phases = synth.phases();
            meter.ignitions = synth.ignitions();
            meter.peak = peak;
            meter.stream = status;
            meter.deviceError = error;
            meter.underruns = underruns;
            auto split = history.begin() + static_cast<std::ptrdiff_t>(writePosition);
            const auto middle = std::copy(split, history.end(), meter.mono.begin());
            std::copy(history.begin(), split, middle);
            ++meter.serial;
        };

        auto makeBlock = [&] {
            synth.render(frames);
            if (synth.state().mode != EngineMode::Off) tail = 16;
            else tail = std::max(0, tail - 1);
            peak *= 0.929;
            for (const auto& sample : frames) {
                history[writePosition] = 0.5f * (sample.left + sample.right);
                writePosition = (writePosition + 1) % scopeFrames;
                peak = std::max(peak, static_cast<double>(std::max(std::abs(sample.left), std::abs(sample.right))));
            }
            if (++blocks % 4 == 0) publish(stream.active() ? StreamStatus::Streaming : StreamStatus::NoDevice);
        };

        while (!quit.load(std::memory_order_relaxed)) {
            Settings settings;
            {
                std::scoped_lock lock(settingsMutex);
                settings = current;
            }
            const auto& controls = settings.controls;
            const bool newProgram = controls.program != previousProgram || controls.programToken != previousToken;
            synth.apply(settings);
            const auto wantsSound = [&] {
                const bool commanded = controls.program == Program::Manual ? controls.running : newProgram || synth.state().progress < 1.0;
                return commanded || synth.state().mode != EngineMode::Off || tail > 0;
            };
            if (!wantsSound()) {
                if (stream.active() && !stream.empty()) {
                    const HANDLE events[]{wake.handle, stream.event()};
                    if (WaitForMultipleObjects(2, events, FALSE, 500) != WAIT_TIMEOUT) continue;
                }
                stream.close();
                streaming = false;
                error = 0;
                peak = 0;
                history = {};
                publish(StreamStatus::Standby);
                WaitForSingleObject(wake.handle, INFINITE);
                continue;
            }

            const auto now = Clock::now();
            if (!stream.active() && now >= retry) {
                error = stream.open();
                fallback = now;
                if (error) {
                    retry = now + 1400ms;
                    publish(StreamStatus::NoDevice);
                }
            }

            if (stream.active()) {
                if (streaming && stream.empty() && synth.state().mode != EngineMode::Off) ++underruns;
                for (std::size_t i = 0; i < numBuffers && stream.hasRoom() && wantsSound(); ++i) {
                    makeBlock();
                    error = stream.write(frames);
                    if (error) break;
                    streaming = true;
                }
                previousProgram = controls.program;
                previousToken = controls.programToken;
                if (!error) {
                    const HANDLE events[]{wake.handle, stream.event()};
                    if (WaitForMultipleObjects(2, events, FALSE, 500) != WAIT_TIMEOUT) continue;
                    error = MMSYSERR_NODRIVER;
                }
                stream.close();
                streaming = false;
                retry = Clock::now() + 700ms;
                fallback = Clock::now();
                publish(StreamStatus::NoDevice);
            } else {
                if (fallback + 32ms < now) fallback = now;
                if (now >= fallback) {
                    makeBlock();
                    fallback += 8ms;
                    previousProgram = controls.program;
                    previousToken = controls.programToken;
                }
                const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(fallback - Clock::now()).count();
                WaitForSingleObject(wake.handle, static_cast<DWORD>(std::clamp<std::int64_t>(remaining, 1, 8)));
            }
        }
        stream.close();
        if (priority) AvRevertMmThreadCharacteristics(priority);
    }
};

AudioOutput::AudioOutput() : worker_(std::make_unique<Worker>()) {}
AudioOutput::~AudioOutput() = default;

void AudioOutput::submit(const Settings& settings) {
    {
        std::scoped_lock lock(worker_->settingsMutex);
        worker_->current = settings;
    }
    if (worker_->wake.handle) SetEvent(worker_->wake.handle);
}

AudioSnapshot AudioOutput::snapshot() const {
    std::scoped_lock lock(worker_->meterMutex);
    return worker_->meter;
}

} // namespace esl
