from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout

class CvConan(ConanFile):
    name = "cv"
    version = "1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("spdlog/1.15.1")
        self.requires("fmt/11.1.3")
        self.requires("nlohmann_json/3.11.3")
        self.requires("yaml-cpp/0.8.0")
        self.requires("opencv/4.11.0")

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
