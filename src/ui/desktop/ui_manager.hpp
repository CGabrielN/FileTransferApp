#pragma once

#include <QMainWindow>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QSystemTrayIcon>

#include "../../core/discovery_service.hpp"
#include "../../core/transfer_manager.hpp"
#include "../../core/file_handler.hpp"

#include <memory>
#include <vector>
#include <mutex>

// Forward declarations
class QTableWidgetItem;
class QMenu;
class QAction;
class QVBoxLayout;
class QSplitter;
class QProgressBar;

class UIManager : public QMainWindow {
    Q_OBJECT

public:
    /**
     * Constructor
     * @param discoveryService Discovery service for finding peers
     * @param transferManager Manager for handling file transfers
     * @param fileHandler Handler for file operations
     * @param parent Parent widget
     */
    UIManager(std::shared_ptr<core::DiscoveryService> discoveryService,
              std::shared_ptr<core::TransferManager> transferManager,
              std::shared_ptr<core::FileHandler> fileHandler,
              QWidget* parent = nullptr);

    /**
     * Destructor
     */
    ~UIManager();

    /**
     * Initialize the UI
     * @return True if initialization was successful, false otherwise
     */
    bool init();

    /**
     * Shutdown the UI
     */
    void shutdown();

protected:
    /**
     * Override close event to minimize to tray if tray icon is enabled
     */
    void closeEvent(QCloseEvent* event) override;

private slots:
            /**
             * Update UI data from services
             */
            void updateData();

    /**
     * Send a file to a peer
     */
    void sendFileToPeer();

    /**
     * Show about dialog
     */
    void showAboutDialog();

    /**
     * Change display name
     */
    void changeDisplayName();

    /**
     * Change download directory
     */
    void changeDownloadDirectory();

    /**
     * Cancel selected transfer
     */
    void cancelSelectedTransfer();

    /**
     * Open selected file
     */
    void openSelectedFile();

    /**
     * Open containing folder of selected file
     */
    void openContainingFolder();

    /**
     * Handle tray icon activation
     */
    void trayIconActivated(QSystemTrayIcon::ActivationReason reason);

    /**
     * Show/Hide main window
     */
    void toggleWindow();

    /**
     * Handle selection changed in peer table
     */
    void peerSelectionChanged();

    /**
     * Handle selection changed in transfer table
     */
    void transferSelectionChanged();

private:
    // Services
    std::shared_ptr<core::DiscoveryService> m_discoveryService;
    std::shared_ptr<core::TransferManager> m_transferManager;
    std::shared_ptr<core::FileHandler> m_fileHandler;

    // QT UI components
    QTableWidget* m_peerTable;
    QTableWidget* m_transferTable;
    QLabel* m_statusLabel;
    QTextEdit* m_transferDetails;
    QPushButton* m_sendFileButton;
    QPushButton* m_cancelTransferButton;
    QPushButton* m_openFileButton;
    QPushButton* m_openFolderButton;
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_trayMenu;
    QTimer* m_updateTimer;

    // Data
    std::mutex m_peersMutex;
    std::vector<core::PeerInfo> m_peers;
    int m_selectedPeerIndex;

    std::mutex m_transfersMutex;
    std::vector<core::TransferInfo> m_transfers;
    int m_selectedTransferIndex;

    // State
    bool m_initialized;

    /**
     * Create main UI components
     */
    void createMainUI();

    /**
     * Create menu bar
     */
    void createMenuBar();

    /**
     * Create tray icon
     */
    void createTrayIcon();

    /**
     * Create status bar
     */
    void createStatusBar();

    /**
     * Update peer table with current data
     */
    void updatePeerTable();

    /**
     * Update transfer table with current data
     */
    void updateTransferTable();

    /**
     * Update transfer details text with information about the selected transfer
     */
    void updateTransferDetails();

    /**
     * Update UI button states based on selection
     */
    void updateButtonStates();

    /**
     * Handle response to a transfer request
     */
    bool onTransferRequest(const core::TransferInfo& transfer);

    /**
     * Register callbacks with services
     */
    void registerServiceCallbacks();

    /**
     * Get a human-readable string for a transfer status
     */
    QString getStatusString(core::TransferStatus status);

    /**
     * Format a timestamp into a human-readable string
     */
    QString formatTimestamp(int64_t timestamp);

    /**
     * Format a duration into a human-readable string
     */
    QString formatDuration(int64_t milliseconds);

    /**
     * Get a color for a transfer status
     */
    QColor getStatusColor(core::TransferStatus status);
};