// Platform-specific UI include will be handled by CMake
#ifdef PLATFORM_WINDOWS
#include "ui/desktop/ui_manager.hpp"
#elif defined(PLATFORM_MACOS)
#include "ui/desktop/macos_ui.hpp"
#elif defined(PLATFORM_LINUX)
#include "ui/desktop/ui_manager.hpp"
#elif defined(PLATFORM_ANDROID)
#include "ui/mobile/android_ui.hpp"
#elif defined(PLATFORM_IOS)
#include "ui/mobile/ios_ui.hpp"
#endif
#include "ui/desktop/windows_main.cpp"

int main(int argc, char* argv[]) {
    WindowsMain::main(argc, argv);
}