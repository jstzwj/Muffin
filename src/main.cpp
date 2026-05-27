#include "app/Application.h"
#include "app/MainWindow.h"

int main(int argc, char* argv[])
{
    Muffin::Application app(argc, argv);

    app.setApplicationName("Muffin");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("Muffin");

    Muffin::MainWindow window;
    window.show();

    return app.exec();
}
