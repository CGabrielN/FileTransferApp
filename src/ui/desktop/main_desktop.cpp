#include "utils/logging.hpp"
#include "platform/platform.hpp"
#include "core/discovery_service.hpp"
#include "core/file_handler.hpp"
#include "core/transfer_manager.hpp"
#include "ui/desktop/ui_manager.hpp"

#include <QApplication>
#include <QSplashScreen>
#include <QIcon>
#include <QPixmap>
#include <QObject>
#include <iostream>
#include <memory>
#include <string>
#include "spdlog/spdlog.h"
#include "utils/encryption.hpp"

int main(int argc, char* argv[]) {
    try {
        // Initialize Qt application
        QApplication app(argc, argv);

        // Initialize resources
        Q_INIT_RESOURCE(resources);

        // Set application properties
        app.setApplicationName("FileTransferApp");
        app.setApplicationDisplayName("File Transfer App");
        app.setApplicationVersion("1.0.0");
        app.setOrganizationDomain("filetransferapp.example.com");

        // Set application icon
        QIcon appIcon(":/app-icon.png");
        app.setWindowIcon(appIcon);

        // Create splash screen
        QSplashScreen splash(QPixmap(":/app-icon.png"));
        splash.show();
        app.processEvents();

        // Initialize logging
        utils::Logging::init("FileTransferApp", true, true, spdlog::level::debug);

        SPDLOG_INFO("Starting File Transfer App v1.0.0");
        splash.showMessage("Initializing application...", Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
        app.processEvents();

        // Create platform implementation
        auto platform = platform::PlatformFactory::create();
        SPDLOG_INFO("Platform: {}", platform->getName());
        splash.showMessage("Initializing platform...", Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
        app.processEvents();

        // Create socket handler
        auto socketHandler = std::make_shared<network::SocketHandler>();
        splash.showMessage("Initializing network...", Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
        app.processEvents();

        // Create file handler
        auto fileHandler = std::make_shared<core::FileHandler>(platform);

        // Create discovery service
        auto discoveryService = std::make_shared<core::DiscoveryService>(
                socketHandler,
                platform
        );

        // Set display name - use computer name or username by default
        std::string displayName = "User on " + platform->getName();
        discoveryService->setDisplayName(displayName);
        splash.showMessage("Starting discovery service...", Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
        app.processEvents();

        // Create transfer manager
        auto transferManager = std::make_shared<core::TransferManager>(
                fileHandler,
                socketHandler,
                discoveryService
        );

        // Initialize transfer manager
        if (!transferManager->init()) {
            SPDLOG_ERROR("Failed to initialize transfer manager");
            return 1;
        }

#ifdef ENABLE_ENCRYPTION
            // Enable encryption
            transferManager->setEncryptionEnabled(true);

            // Set a default password or prompt for one
            std::string password = "a-secure-password";
            transferManager->setEncryptionPassword(password);

            SPDLOG_INFO("File transfer encryption enabled");
#endif

        splash.showMessage("Initializing GUI...", Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
        app.processEvents();

        // Start discovery service
        discoveryService->start();

        // Create and initialize UI manager
        auto uiManager = new UIManager(
                discoveryService,
                transferManager,
                fileHandler
        );

        if (!uiManager->init()) {
            SPDLOG_ERROR("Failed to initialize UI manager");
            return 1;
        }

        // Setup cleanup on application exit
        QObject::connect(&app, &QApplication::aboutToQuit, [&]() {
            uiManager->shutdown();
            transferManager->shutdown();
            discoveryService->stop();
            socketHandler->shutdown();

#ifdef ENABLE_ENCRYPTION
            utils::Encryption::shutdown();
#endif

            SPDLOG_INFO("File Transfer App shutdown complete");
            utils::Logging::shutdown();
        });

        // Show main window and close splash
        uiManager->show();
        splash.finish(uiManager);

        // Run application
        return app.exec();

    } catch (const std::exception &e) {
        SPDLOG_CRITICAL("Unhandled exception: {}", e.what());
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}