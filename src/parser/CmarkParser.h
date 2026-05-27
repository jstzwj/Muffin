#pragma once
#include "AstTree.h"
#include <QString>
#include <memory>

namespace Muffin {

class CmarkParser {
public:
    CmarkParser();
    ~CmarkParser();

    AstTree parse(const QString& markdown);
    AstTree parse(const QByteArray& utf8Data);

private:
    void ensureExtensionsRegistered();

    static bool s_extensionsRegistered;
};

} // namespace Muffin
