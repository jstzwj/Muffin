#pragma once
#include <cmark-gfm.h>
#include <QString>
#include <cstdlib>

namespace Muffin {

class SafeCString {
public:
    explicit SafeCString(char* cstr) : m_data(cstr) {}
    ~SafeCString() { std::free(m_data); }

    SafeCString(const SafeCString&) = delete;
    SafeCString& operator=(const SafeCString&) = delete;

    const char* data() const { return m_data; }
    bool isNull() const { return !m_data; }
    QString toQString() const { return m_data ? QString::fromUtf8(m_data) : QString(); }

private:
    char* m_data;
};

} // namespace Muffin
