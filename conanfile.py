from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class MuffinConan(ConanFile):
    name = "muffin"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    exports_sources = "CMakeLists.txt", "src/*", "resources/*"

    def requirements(self):
        self.requires("qt/[>=6.5 <7]", options={
            "shared": True,
            "with_pq": False,
            "with_odbc": False,
        })

    def build_requirements(self):
        self.tool_requires("cmake/[>=3.24]")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def layout(self):
        cmake_layout(self)
