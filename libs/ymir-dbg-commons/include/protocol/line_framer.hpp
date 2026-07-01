#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace ymir::debug {

enum class LineFramerError {
    LineTooLong,
};

class LineFramer {
public:
    static constexpr size_t kMaxLineLength = 1024 * 1024; // 1 MiB

    using LineCallback = std::function<void(std::string_view)>;
    using ErrorCallback = std::function<void(LineFramerError)>;

    LineFramer(LineCallback onLine, ErrorCallback onError)
        : m_onLine(std::move(onLine))
        , m_onError(std::move(onError)) {}

    void Push(const char *data, size_t length) {
        for (size_t i = 0; i < length; ++i) {
            const char c = data[i];
            if (c == '\r') {
                EmitLine();
                m_lastWasCarriageReturn = true;
                continue;
            }
            if (c == '\n') {
                if (!m_lastWasCarriageReturn) {
                    EmitLine();
                }
                m_lastWasCarriageReturn = false;
                continue;
            }

            m_lastWasCarriageReturn = false;
            if (m_droppingOversizedLine) {
                // Drain remaining bytes of the oversized line until the next terminator.
                continue;
            }
            if (m_buffer.length() >= kMaxLineLength) {
                m_onError(LineFramerError::LineTooLong);
                m_droppingOversizedLine = true;
            } else {
                m_buffer += c;
            }
        }
    }

private:
    void EmitLine() {
        if (m_droppingOversizedLine) {
            m_droppingOversizedLine = false;
        } else if (!m_buffer.empty()) {
            m_onLine(m_buffer);
        }
        m_buffer.clear();
    }

    std::string m_buffer;
    bool m_droppingOversizedLine{};
    bool m_lastWasCarriageReturn{};
    LineCallback m_onLine;
    ErrorCallback m_onError;
};

} // namespace ymir::debug
