#pragma once

#include <QApplication>

namespace Muffin {

class Application : public QApplication
{
    Q_OBJECT

public:
    Application(int& argc, char** argv);
};

} // namespace Muffin
