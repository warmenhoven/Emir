#pragma once

#include <util/service_locator.hpp>

#include "midi_types.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#if !defined(__APPLE__) && !defined(__FreeBSD__)
    #define YMIR_MIDI_ASYNC_INIT
#endif

// Apple's and FreeBSD's libc++ does not support std::atomic<std::shared_ptr<T>>.
// Since MIDI initialization on those systems never seem to cause problems, we'll just not use threads.
#if defined(YMIR_MIDI_ASYNC_INIT)
    #include <atomic>
#endif

namespace app::services {

/// @brief Provides access to real-time MIDI inputs and outputs through RtMidi.
class MIDIService {
public:
    MIDIService(util::ServiceLocator &serviceLocator);
    ~MIDIService();

    void Initialize(std::function<void()> onComplete);

private:
    void DoInit(std::function<void()> onComplete);

public:
    std::string GetMidiVirtualInputPortName() const;
    std::string GetMidiVirtualOutputPortName() const;

    std::string GetMidiInputPortName() const;
    std::string GetMidiOutputPortName() const;

    int FindInputPortByName(std::string name) const;
    int FindOutputPortByName(std::string name) const;

    std::shared_ptr<util::IRtMidiIn> GetInput() const;
    std::shared_ptr<util::IRtMidiOut> GetOutput() const;

private:
    void SetInput(std::shared_ptr<util::IRtMidiIn> input);
    void SetOutput(std::shared_ptr<util::IRtMidiOut> output);

public:
    void SetMidiInputCallback(RtMidiIn::RtMidiCallback callback, void *userData = nullptr);

private:
    util::ServiceLocator &m_serviceLocator;

#if defined(YMIR_MIDI_ASYNC_INIT)
    std::atomic<std::shared_ptr<util::IRtMidiIn>> m_input;
    std::atomic<std::shared_ptr<util::IRtMidiOut>> m_output;
#else
    std::shared_ptr<util::IRtMidiIn> m_input;
    std::shared_ptr<util::IRtMidiOut> m_output;
#endif

    std::mutex m_mtxCallback;
    RtMidiIn::RtMidiCallback m_inputCallback;
    void *m_inputCallbackUserData;
};

} // namespace app::services
