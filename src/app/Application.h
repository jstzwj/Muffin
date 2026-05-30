#pragma once

#include <QApplication>

class Application : public QApplication {
    Q_OBJECT
public:
    explicit Application(int &argc, char **argv);
};
