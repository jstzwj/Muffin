from conan import ConanFile
from conan.tools.cmake import cmake_layout


class MuffinDependencies(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"
    default_options = {
        "qt/*:shared": True,
        "qt/*:widgets": True,
        "qt/*:gui": True,
        "qt/*:qtsvg": True,
        "qt/*:qtdeclarative": False,
        "qt/*:with_odbc": False,
        "qt/*:with_pq": False,
        "icu/*:shared": False,
    }

    def configure(self):
        if self.settings.os != "Windows":
            # CMake deploys ICU's matching official .dat and empty data stub.
            # Keep Conan from exporting ICU_DATA process-wide, which would
            # also redirect Apple's incompatible system ICU.
            self.options["icu/*"].data_packaging = "static"

    def requirements(self):
        self.requires("qt/6.11.1")
        # Qt's recipe pins 10.42. Use one PCRE2 version throughout the graph,
        # including Qt/GLib and Muffin's native workspace matcher.
        self.requires("pcre2/10.49", force=True)
        # Direct requirement also emits the config needed by Qt's dependency.
        self.requires("harfbuzz/12.3.0")
        self.requires("freetype/2.13.2")
        self.requires("icu/74.2")
        self.requires("libwebp/1.6.0")
        self.requires("dav1d/1.5.3")
        self.requires("libjpeg/9f")
        self.requires("libpng/1.6.51")
        self.requires("lexbor/2.6.0")
        self.requires("yoga/3.2.0")
        self.requires("yaml-cpp/0.9.0")

    def layout(self):
        cmake_layout(self)
