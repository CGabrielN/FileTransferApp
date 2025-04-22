////// Platform-specific UI include will be handled by CMake
////#ifdef PLATFORM_WINDOWS
////#include "ui/desktop/ui_manager.hpp"
////#elif defined(PLATFORM_MACOS)
////#include "ui/desktop/macos_ui.hpp"
////#elif defined(PLATFORM_LINUX)
////#include "ui/desktop/ui_manager.hpp"
////#elif defined(PLATFORM_ANDROID)
////#include "ui/mobile/android_ui.hpp"
////#elif defined(PLATFORM_IOS)
////#include "ui/mobile/ios_ui.hpp"
////#endif
////#include "ui/desktop/windows_main.cpp"
////
////int main(int argc, char* argv[]) {
////    WindowsMain::main(argc, argv);
////}
//
//#include "platform/platform.hpp"
//#include "core/discovery_service.hpp"
//#include "core/file_handler.hpp"
//#include "core/transfer_manager.hpp"
//#include "utils/logging.hpp"
//
//// Platform-specific UI include will be handled based on the platform
//#ifdef PLATFORM_WINDOWS
//#include "ui/desktop/ui_manager.hpp"
//#elif defined(PLATFORM_MACOS)
//#include "ui/desktop/ui_manager.hpp"
//#elif defined(PLATFORM_LINUX)
//#include "ui/desktop/ui_manager.hpp"
//#elif defined(PLATFORM_ANDROID)
//#include "ui/mobile/android_ui.hpp"
//#elif defined(PLATFORM_IOS)
//#include "ui/mobile/ios_ui.hpp"
//#endif
//
//#include <QApplication>
//#include <QSplashScreen>
//#include <QPixmap>
//#include <QThread>
//#include <iostream>
//#include <memory>
//#include <string>
//#include <spdlog/spdlog.h>
//
//int main(int argc, char* argv[]) {
//    try {
//        // Initialize Qt application
//        QApplication app(argc, argv);
//
//        // Initialize resources
//        Q_INIT_RESOURCE(resources);
//
//        app.setApplicationName("FileTransferApp");
//        app.setApplicationDisplayName("File Transfer App");
//        app.setApplicationVersion("1.0.0");
//        app.setOrganizationDomain("filetransferapp.example.com");
//
//        // Set application icon
//        QIcon appIcon(":/app-icon.png");
//        app.setWindowIcon(appIcon);
//
//        // Create splash screen
//        QSplashScreen splash(QPixmap(":/app-icon.png"));
//        splash.show();
//        app.processEvents();
//
//        // Initialize logging
//        utils::Logging::init("FileTransferApp", true, true, spdlog::level::debug);
//
//        SPDLOG_INFO("Starting File Transfer App v1.0.0");
//        splash.showMessage("Initializing application...", Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
//        app.processEvents();
//
//        // Create platform implementation
//        auto platform = platform::PlatformFactory::create();
//        SPDLOG_INFO("Platform: {}", platform->getName());
//        splash.showMessage("Initializing platform...", Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
//        app.processEvents();
//
//        // Create socket handler
//        auto socketHandler = std::make_shared<network::SocketHandler>();
//        splash.showMessage("Initializing network...", Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
//        app.processEvents();
//
//        // Create file handler
//        auto fileHandler = std::make_shared<core::FileHandler>(platform);
//
//        // Create discovery service
//        auto discoveryService = std::make_shared<core::DiscoveryService>(
//                socketHandler,
//                platform
//        );
//
//        // Set display name - use computer name or username by default
//        std::string displayName = "User on " + platform->getName();
//        discoveryService->setDisplayName(displayName);
//        splash.showMessage("Starting discovery service...", Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
//        app.processEvents();
//
//        // Create transfer manager
//        auto transferManager = std::make_shared<core::TransferManager>(
//                fileHandler,
//                socketHandler,
//                discoveryService
//        );
//
//        // Initialize transfer manager
//        if (!transferManager->init()) {
//            SPDLOG_ERROR("Failed to initialize transfer manager");
//            return 1;
//        }
//        splash.showMessage("Initializing GUI...", Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
//        app.processEvents();
//
//        // Start discovery service
//        discoveryService->start();
//
//        // Create and initialize UI manager
//        auto uiManager = new UIManager(
//                discoveryService,
//                transferManager,
//                fileHandler
//        );
//
//        if (!uiManager->init()) {
//            SPDLOG_ERROR("Failed to initialize UI manager");
//            return 1;
//        }
//
//        // Setup cleanup on application exit
//        QObject::connect(&app, &QApplication::aboutToQuit, [&]() {
//            uiManager->shutdown();
//            transferManager->shutdown();
//            discoveryService->stop();
//            socketHandler->shutdown();
//
//            SPDLOG_INFO("File Transfer App shutdown complete");
//            utils::Logging::shutdown();
//        });
//
//        // Show main window and close splash
//        uiManager->show();
//        splash.finish(uiManager);
//
//        // Run application
//        return app.exec();
//
//    } catch (const std::exception &e) {
//        SPDLOG_CRITICAL("Unhandled exception: {}", e.what());
//        std::cerr << "ERROR: " << e.what() << std::endl;
//        return 1;
//    }
//}