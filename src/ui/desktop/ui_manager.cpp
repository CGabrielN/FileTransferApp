#include "ui_manager.hpp"

#include <QMainWindow>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QMenuBar>
#include <QMenu>
#include <QPainter>
#include <QAction>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QTimer>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QProgressBar>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <QPainter>

namespace fs = std::filesystem;


UIManager::UIManager(std::shared_ptr<core::DiscoveryService> discoveryService,
                     std::shared_ptr<core::TransferManager> transferManager,
                     std::shared_ptr<core::FileHandler> fileHandler, QWidget *parent)
        : QMainWindow(parent), m_discoveryService(std::move(discoveryService)),
          m_transferManager(std::move(transferManager)),
          m_fileHandler(std::move(fileHandler)), m_initialized(false), m_selectedPeerIndex(-1),
          m_selectedTransferIndex(-1) {
    // Set window title and size
    setWindowTitle("File Transfer App");
    resize(1024, 768);
}


UIManager::~UIManager() {
    shutdown();
}

bool UIManager::init() {
    if (m_initialized) {
        SPDLOG_WARN("UI Manager already initialized");
        return true;
    }

    SPDLOG_INFO("Initializing UI Manager");

    // Create UI components
    createMainUI();
    createMenuBar();
    createStatusBar();
    createTrayIcon();

    // Register callbacks with services
    registerServiceCallbacks();

    // Start update timer
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &UIManager::updateData);
    m_updateTimer->start(1000); // update every second

    // Initial update
    updateData();

    m_initialized = true;
    SPDLOG_INFO("UI Manager initialized successfully");
    return true;
}


void UIManager::shutdown() {
    if (!m_initialized) {
        return;
    }

    SPDLOG_INFO("Shutting down UI Manager");

    if (m_updateTimer) {
        m_updateTimer->stop();
    }

    m_initialized = false;
    SPDLOG_INFO("UI Manager shutdown complete");
}

void UIManager::closeEvent(QCloseEvent *event) {
    if (m_initialized && m_trayIcon->isVisible()) {
        QMessageBox::information(this, "File Transfer App",
                                 "The application will continue running in the system tray.\n"
                                 "To terminate the program, right-click the tray icon and select Exit.");
        hide();
        event->ignore();
    } else {
        QMainWindow::closeEvent(event);
    }
}


void UIManager::createMainUI() {
    // Create central widget and main layout
    auto *centralWidget = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(centralWidget);

    // Create splitter for top and bottom sections
    auto *mainSplitter = new QSplitter(Qt::Vertical, centralWidget);
    mainLayout->addWidget(mainSplitter);

    // Create peers section
    auto *peersWidget = new QWidget(mainSplitter);
    auto *peersLayout = new QVBoxLayout(peersWidget);
    peersLayout->setContentsMargins(5, 5, 5, 5);

    auto *peersLabel = new QLabel("Available Peers", peersWidget);
    peersLayout->addWidget(peersLabel);


    m_peerTable = new QTableWidget(peersWidget);
    m_peerTable->setColumnCount(3);
    m_peerTable->setHorizontalHeaderLabels({"Name", "Platform", "IP Address"});
    m_peerTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_peerTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_peerTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_peerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_peerTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_peerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_peerTable->setAlternatingRowColors(true);
    peersLayout->addWidget(m_peerTable);

    m_sendFileButton = new QPushButton("Send File to Selected Peer", peersWidget);
    m_sendFileButton->setEnabled(false);
    peersLayout->addWidget(m_sendFileButton);


    // Create transfers section
    auto *transfersWidget = new QWidget(mainSplitter);
    auto *transfersLayout = new QVBoxLayout(transfersWidget);
    transfersLayout->setContentsMargins(5, 5, 5, 5);

    auto *transfersLabel = new QLabel("Transfer History", transfersWidget);
    transfersLayout->addWidget(transfersLabel);

    m_transferTable = new QTableWidget(transfersWidget);
    m_transferTable->setColumnCount(5);
    m_transferTable->setHorizontalHeaderLabels({"File Name", "Peer", "Direction", "Status", "Header"});
    m_transferTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_transferTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_transferTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_transferTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_transferTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_transferTable->setColumnWidth(4, 150);
    m_transferTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_transferTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_transferTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_transferTable->setAlternatingRowColors(true);
    transfersLayout->addWidget(m_transferTable);

    // Details and buttons
    auto *detailsWidget = new QWidget(transfersWidget);
    auto *detailsLayout = new QHBoxLayout(detailsWidget);

    m_transferDetails = new QTextEdit(detailsWidget);
    m_transferDetails->setReadOnly(true);
    m_transferDetails->setMinimumHeight(100);
    m_transferDetails->setMaximumHeight(150);
    detailsLayout->addWidget(m_transferDetails, 2);

    auto *buttonWidget = new QWidget(detailsWidget);
    auto *buttonLayout = new QVBoxLayout(buttonWidget);

    m_cancelTransferButton = new QPushButton("Cancel Transfer", buttonWidget);
    m_cancelTransferButton->setEnabled(false);
    buttonLayout->addWidget(m_cancelTransferButton);

    m_openFileButton = new QPushButton("Open File", buttonWidget);
    m_openFileButton->setEnabled(false);
    buttonLayout->addWidget(m_openFileButton);

    m_openFolderButton = new QPushButton("Open Folder", buttonWidget);
    m_openFolderButton->setEnabled(false);
    buttonLayout->addWidget(m_openFolderButton);

    buttonLayout->addStretch();
    detailsLayout->addWidget(buttonWidget, 1);

    transfersLayout->addWidget(detailsWidget);

    // Set splitter sizes
    mainSplitter->setSizes({300, 400});

    // Connect signals and slots
    connect(m_peerTable, &QTableWidget::itemSelectionChanged, this, &UIManager::peerSelectionChanged);
    connect(m_transferTable, &QTableWidget::itemSelectionChanged, this, &UIManager::transferSelectionChanged);
    connect(m_sendFileButton, &QPushButton::clicked, this, &UIManager::sendFileToPeer);
    connect(m_cancelTransferButton, &QPushButton::clicked, this, &UIManager::cancelSelectedTransfer);
    connect(m_openFileButton, &QPushButton::clicked, this, &UIManager::openSelectedFile);
    connect(m_openFolderButton, &QPushButton::clicked, this, &UIManager::openContainingFolder);

    setCentralWidget(centralWidget);
}


void UIManager::createMenuBar() {
    // File menu
    QMenu *fileMenu = menuBar()->addMenu("File");

    QAction *sendFileAction = fileMenu->addAction("Send File...");
    sendFileAction->setShortcut(QKeySequence::Open);
    connect(sendFileAction, &QAction::triggered, this, &UIManager::sendFileToPeer);

    fileMenu->addSeparator();

    QAction *exitAction = fileMenu->addAction("Exit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

    // Settings menu
    QMenu *settingsMenu = menuBar()->addMenu("Settings");

    QAction *displayNameAction = settingsMenu->addAction("Change Display Name...");
    connect(displayNameAction, &QAction::triggered, this, &UIManager::changeDisplayName);

    QAction *downloadDirAction = settingsMenu->addAction("Change Download Directory...");
    connect(downloadDirAction, &QAction::triggered, this, &UIManager::changeDownloadDirectory);

    // Help menu
    QMenu *helpMenu = menuBar()->addMenu("Help");

    QAction *aboutAction = helpMenu->addAction("About");
    connect(aboutAction, &QAction::triggered, this, &UIManager::showAboutDialog);
}


void UIManager::createStatusBar() {
    m_statusLabel = new QLabel("Ready");
    statusBar()->addWidget(m_statusLabel);
}

void UIManager::createTrayIcon() {
    // Create tray icon if supported
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        // Create tray menu
        m_trayMenu = new QMenu(this);

        QAction *showHideAction = m_trayMenu->addAction("Show/Hide");
        connect(showHideAction, &QAction::triggered, this, &UIManager::toggleWindow);

        m_trayMenu->addSeparator();

        QAction *exitAction = m_trayMenu->addAction("Exit");
        connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

        m_trayIcon = new QSystemTrayIcon(this);
        // Create tray icon
        QIcon trayIcon;


        if (QFile::exists(":/app-icon.png")) {
            trayIcon = QIcon(":/app-icon.png");
        } else {
            trayIcon = static_cast<QIcon>(nullptr);
        }

        m_trayIcon->setIcon(trayIcon);
        m_trayIcon->setToolTip("File Transfer App");
        m_trayIcon->setContextMenu(m_trayMenu);

        connect(m_trayIcon, &QSystemTrayIcon::activated, this, &UIManager::trayIconActivated);

        m_trayIcon->show();

        SPDLOG_INFO("Tray icon initialized successfully");
    } else {
        SPDLOG_WARN("System tray is not available on this platform");
    }
}


void UIManager::updateData() {
    // Update peers
    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        m_peers = m_discoveryService->getKnownPeers();
    }
    updatePeerTable();

    // Update transfers
    {
        std::lock_guard<std::mutex> lock(m_transfersMutex);
        m_transfers = m_transferManager->getAllTransfers();
    }
    updateTransferTable();

    // Update selected transfer details
    updateTransferDetails();

    // Update button states
    updateButtonStates();

    // Update status bar
    int peerCount = m_peers.size();
    int transfersCount = m_transfers.size();

    QString statusText = QString("Ready | %1 peer%2 | %3 transfer%4")
            .arg(peerCount)
            .arg(peerCount != 1 ? "s" : "")
            .arg(transfersCount)
            .arg(transfersCount != 1 ? "s" : "");

    m_statusLabel->setText(statusText);
}


void UIManager::updatePeerTable() {
    // Disconnect to prevent signals during update
    m_peerTable->blockSignals(true);

    // Remember selected peer ID
    QString selectedPeerId;
    if (m_selectedPeerIndex >= 0 && m_selectedPeerIndex < static_cast<int>(m_peers.size())) {
        selectedPeerId = QString::fromStdString(m_peers[m_selectedPeerIndex].id);
    }

    // Clear and resize the table
    m_peerTable->clearContents();
    m_peerTable->setRowCount(m_peers.size());

    // Fill table with peers
    int newSelectedRow = -1;
    for (size_t i = 0; i < m_peers.size(); ++i) {
        const auto &peer = m_peers[i];

        // Add peer name
        auto *nameItem = new QTableWidgetItem(QString::fromStdString(peer.name));
        m_peerTable->setItem(i, 0, nameItem);

        // Add platform
        auto *platformItem = new QTableWidgetItem(QString::fromStdString(peer.platform));
        m_peerTable->setItem(i, 1, platformItem);

        // Add IP address
        QTableWidgetItem *ipItem = new QTableWidgetItem(QString::fromStdString(peer.ipAddress));
        m_peerTable->setItem(i, 2, ipItem);

        // Check if this is the previously selected peer
        if (!selectedPeerId.isEmpty() && selectedPeerId == QString::fromStdString(peer.id)) {
            newSelectedRow = i;
        }
    }

    // Restore selection
    if (newSelectedRow >= 0) {
        m_peerTable->selectRow(newSelectedRow);
        m_selectedPeerIndex = newSelectedRow;
    } else {
        m_selectedPeerIndex = -1;
    }

    // Re-enable signals
    m_peerTable->blockSignals(false);
}

void UIManager::updateTransferTable() {
    // Disconnect to prevent signals during update
    m_transferTable->blockSignals(true);

    // Remember selected transfer ID
    QString selectedTransferId;
    if (m_selectedTransferIndex >= 0 && m_selectedTransferIndex < static_cast<int>(m_transfers.size())) {
        selectedTransferId = QString::fromStdString(m_transfers[m_selectedTransferIndex].id);
    }

    // Clear and resize the table
    m_transferTable->clearContents();
    m_transferTable->setRowCount(m_transfers.size());

    // Fill table with transfers
    int newSelectedRow = -1;
    for (size_t i = 0; i < m_transfers.size(); ++i) {
        const auto &transfer = m_transfers[i];

        // Add file name
        QTableWidgetItem *nameItem = new QTableWidgetItem(QString::fromStdString(transfer.fileName));
        m_transferTable->setItem(i, 0, nameItem);

        // Add peer name
        QTableWidgetItem *peerItem = new QTableWidgetItem(QString::fromStdString(transfer.peerName));
        m_transferTable->setItem(i, 1, peerItem);

        // Add direction
        QString direction = transfer.direction == core::TransferDirection::Incoming ? "Incoming" : "Outgoing";
        QTableWidgetItem *dirItem = new QTableWidgetItem(direction);
        m_transferTable->setItem(i, 2, dirItem);

        // Add status with color
        QString status = getStatusString(transfer.status);
        QTableWidgetItem *statusItem = new QTableWidgetItem(status);
        statusItem->setForeground(getStatusColor(transfer.status));
        statusItem->setTextAlignment(Qt::AlignCenter);
        m_transferTable->setItem(i, 3, statusItem);

        // Add progress
        QProgressBar *progressBar = new QProgressBar();
        progressBar->setMinimum(0);
        progressBar->setMaximum(100);
        progressBar->setValue(static_cast<int>(transfer.progress));
        progressBar->setTextVisible(true);
        progressBar->setFormat("%p%");

        if (transfer.status == core::TransferStatus::Completed) {
            progressBar->setValue(100);
        } else if (transfer.status == core::TransferStatus::Failed ||
                   transfer.status == core::TransferStatus::Canceled) {
            progressBar->setFormat("%v%");
        }

        m_transferTable->setCellWidget(i, 4, progressBar);

        // Check if this is the previously selected transfer
        if (!selectedTransferId.isEmpty() && selectedTransferId == QString::fromStdString(transfer.id)) {
            newSelectedRow = i;
        }
    }

    // Restore selection
    if (newSelectedRow >= 0) {
        m_transferTable->selectRow(newSelectedRow);
        m_selectedTransferIndex = newSelectedRow;
    } else {
        m_selectedTransferIndex = -1;
    }

    // Re-enable signals
    m_transferTable->blockSignals(false);
}


void UIManager::updateTransferDetails() {
    if (m_selectedTransferIndex >= 0 && m_selectedTransferIndex < static_cast<int>(m_transfers.size())) {
        const auto &transfer = m_transfers[m_selectedTransferIndex];

        QString detailsText;
        detailsText += QString("<b>Transfer Details:</b><br>");
        detailsText += QString("<b>File:</b> %1<br>").arg(QString::fromStdString(transfer.fileName));
        detailsText += QString("<b>Path:</b> %1<br>").arg(QString::fromStdString(transfer.filePath));
        detailsText += QString("<b>Size:</b> %1 bytes<br>").arg(transfer.fileSize);
        detailsText += QString("<b>Peer:</b> %1 (%2)<br>").arg(QString::fromStdString(transfer.peerName),
                                                               QString::fromStdString(transfer.peerId));
        detailsText += QString("<b>Started:</b> %1<br>").arg(formatTimestamp(transfer.startTime));

        if (transfer.endTime > 0) {
            detailsText += QString("<b>Ended:</b> %1<br>").arg(formatTimestamp(transfer.endTime));
            detailsText += QString("<b>Duration:</b> %1<br>").arg(
                    formatDuration(transfer.endTime - transfer.startTime));
        }

        if (!transfer.errorMessage.empty()) {
            detailsText += QString("<b><font color='red'>Error:</font></b> %1<br>")
                    .arg(QString::fromStdString(transfer.errorMessage));
        }

        m_transferDetails->setHtml(detailsText);
    } else {
        m_transferDetails->clear();
    }
}


void UIManager::updateButtonStates() {
    // Update send file button state
    m_sendFileButton->setEnabled(m_selectedPeerIndex >= 0);

    // Update transfer action buttons
    bool hasCancelableTransfer = false;
    bool hasCompletedFile = false;

    if (m_selectedTransferIndex >= 0 && m_selectedTransferIndex < static_cast<int>(m_transfers.size())) {
        const auto &transfer = m_transfers[m_selectedTransferIndex];

        // Cancel button enabled for active transfers
        hasCancelableTransfer = (transfer.status == core::TransferStatus::InProgress ||
                                 transfer.status == core::TransferStatus::Waiting ||
                                 transfer.status == core::TransferStatus::Initializing);

        // Open buttons enabled for completed incoming transfers
        hasCompletedFile = (transfer.status == core::TransferStatus::Completed &&
                            transfer.direction == core::TransferDirection::Incoming);
    }

    m_cancelTransferButton->setEnabled(hasCancelableTransfer);
    m_openFileButton->setEnabled(hasCompletedFile);
    m_openFolderButton->setEnabled(hasCompletedFile);
}

void UIManager::sendFileToPeer() {
    // Check if a peer is selected
    if (m_selectedPeerIndex < 0 || m_selectedPeerIndex >= static_cast<int>(m_peers.size())) {
        QMessageBox::warning(this, "Send File", "Please select a peer first.");
        return;
    }

    // Get the peer ID
    const auto &peer = m_peers[m_selectedPeerIndex];
    std::string peerId = peer.id;

    // Open file dialog
    QString filePath = QFileDialog::getOpenFileName(this, "Select File to Send",
                                                    QDir::homePath(), "All Files (*)");

    if (filePath.isEmpty()) {
        return; // User canceled
    }

    // Send the file
    std::string transferId = m_transferManager->sendFile(peerId, filePath.toStdString());

    if (transferId.empty()) {
        QMessageBox::critical(this, "Send File",
                              QString("Failed to send file to %1").arg(QString::fromStdString(peer.name)));
    } else {
        m_statusLabel->setText(QString("Sending file to %1...").arg(QString::fromStdString(peer.name)));
    }
}

void UIManager::cancelSelectedTransfer() {
    if (m_selectedTransferIndex < 0 || m_selectedTransferIndex >= static_cast<int>(m_transfers.size())) {
        return;
    }

    const auto &transfer = m_transfers[m_selectedTransferIndex];

    if (transfer.status != core::TransferStatus::InProgress &&
        transfer.status != core::TransferStatus::Waiting &&
        transfer.status != core::TransferStatus::Initializing) {
        return;
    }

    // Confirm cancel
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Cancel Transfer",
                                                              "Are you sure you want to cancel this transfer?",
                                                              QMessageBox::Yes | QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    // Cancel the transfer
    m_transferManager->cancelTransfer(transfer.id);
}

void UIManager::openSelectedFile() {
    if (m_selectedTransferIndex < 0 || m_selectedTransferIndex >= static_cast<int>(m_transfers.size())) {
        return;
    }

    const auto &transfer = m_transfers[m_selectedTransferIndex];

    if (transfer.status != core::TransferStatus::Completed ||
        transfer.direction != core::TransferDirection::Incoming) {
        return;
    }

    m_fileHandler->openFile(transfer.filePath);
}

void UIManager::openContainingFolder() {
    if (m_selectedTransferIndex < 0 || m_selectedTransferIndex >= static_cast<int>(m_transfers.size())) {
        return;
    }

    const auto &transfer = m_transfers[m_selectedTransferIndex];

    if (transfer.status != core::TransferStatus::Completed ||
        transfer.direction != core::TransferDirection::Incoming) {
        return;
    }

    // Get directory path
    std::string dirPath = fs::path(transfer.filePath).parent_path().string();
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(dirPath)));
}

void UIManager::trayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        toggleWindow();
    }
}

void UIManager::toggleWindow() {
    if (isVisible()) {
        hide();
    } else {
        show();
        raise();
        activateWindow();
    }
}

void UIManager::showAboutDialog() {
    QMessageBox::about(this, "About File Transfer App",
                       "<h3>File Transfer App v1.0.0</h3>"
                       "<p>A cross-platform application for transferring files between devices on a local network.</p>"
                       "<p>Built with Qt and modern C++.</p>");
}


void UIManager::changeDisplayName() {
    QString currentName = QString::fromStdString(m_discoveryService->getDisplayName());

    bool ok;
    QString newName = QInputDialog::getText(this, "Change Display Name",
                                            "Enter new display name:",
                                            QLineEdit::Normal,
                                            currentName, &ok);

    if (ok && !newName.isEmpty()) {
        m_discoveryService->setDisplayName(newName.toStdString());
        m_statusLabel->setText(QString("Display name changed to %1").arg(newName));
        SPDLOG_INFO("Display name changed to: {}", newName.toStdString());
    }
}

void UIManager::changeDownloadDirectory() {
    QString currentDir = QString::fromStdString(m_transferManager->getDefaultDownloadDirectory());

    QString dir = QFileDialog::getExistingDirectory(this, "Select Download Directory",
                                                    currentDir,
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        m_transferManager->setDefaultDownloadDirectory(dir.toStdString());
        m_statusLabel->setText(QString("Download directory changed to %1").arg(dir));
        SPDLOG_INFO("Download directory changed to: {}", dir.toStdString());
    }
}

void UIManager::peerSelectionChanged() {
    QList<QTableWidgetItem *> selectedItems = m_peerTable->selectedItems();

    if (!selectedItems.isEmpty()) {
        m_selectedPeerIndex = selectedItems.first()->row();
    } else {
        m_selectedPeerIndex = -1;
    }

    // Update button state
    m_sendFileButton->setEnabled(m_selectedPeerIndex >= 0);
}

void UIManager::transferSelectionChanged() {
    QList<QTableWidgetItem *> selectedItems = m_transferTable->selectedItems();

    if (!selectedItems.isEmpty()) {
        m_selectedTransferIndex = selectedItems.first()->row();
        updateTransferDetails();
    } else {
        m_selectedTransferIndex = -1;
        m_transferDetails->clear();
    }

    updateButtonStates();
}

void UIManager::registerServiceCallbacks() {
    // Register discovery callbacks
    m_discoveryService->registerPeerDiscoveryCallback(
            [](const core::PeerInfo &peer, bool isNew) {
                // Update will happen on the next timer tick
                Q_UNUSED(peer);
                Q_UNUSED(isNew)
            });

    m_discoveryService->registerPeerLostCallback(
            [](const std::string &peerId) {
                // Update will happen on the next timer tick
                Q_UNUSED(peerId);
            });

    // Register transfer callbacks
    m_transferManager->registerStatusCallback(
            [this](const core::TransferInfo &transfer) {
                // Update will happen on the next timer tick
                Q_UNUSED(transfer);

                // Show notification for completed transfers
                if (transfer.status == core::TransferStatus::Completed && m_trayIcon) {
                    QString title = transfer.direction == core::TransferDirection::Incoming ?
                                    "File Received" : "File Sent";
                    QString message = transfer.direction == core::TransferDirection::Incoming ?
                                      QString("Received file %1 from %2").arg(
                                              QString::fromStdString(transfer.fileName),
                                              QString::fromStdString(transfer.peerName)) :
                                      QString("Sent file %1 to %2").arg(
                                              QString::fromStdString(transfer.fileName),
                                              QString::fromStdString(transfer.peerName));

                    m_trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 5000);
                }
            });

    m_transferManager->registerRequestCallback(
            [this](const core::TransferInfo &transfer) -> bool {
                return onTransferRequest(transfer);
            });
}


bool UIManager::onTransferRequest(const core::TransferInfo &transfer) {
    // Show a dialog asking if the user wants to accept the transfer
    QString message = QString("%1 wants to send you the file:\n\n%2\n\nSize: %3 bytes\n\nAccept?")
            .arg(QString::fromStdString(transfer.peerName))
            .arg(QString::fromStdString(transfer.fileName))
            .arg(transfer.fileSize);

    QMessageBox::StandardButton reply = QMessageBox::question(this, "File Transfer Request",
                                                              message,
                                                              QMessageBox::Yes | QMessageBox::No);

    return reply == QMessageBox::Yes;
}

QString UIManager::getStatusString(core::TransferStatus status) {
    switch (status) {
        case core::TransferStatus::Initializing:
            return "Initializing";
        case core::TransferStatus::Waiting:
            return "Waiting";
        case core::TransferStatus::InProgress:
            return "In Progress";
        case core::TransferStatus::Completed:
            return "Completed";
        case core::TransferStatus::Failed:
            return "Failed";
        case core::TransferStatus::Canceled:
            return "Canceled";
        default:
            return "Unknown";
    }
}

QString UIManager::formatTimestamp(int64_t timestamp) {
    // Convert milliseconds to time_t
    QDateTime dateTime;
    dateTime.setMSecsSinceEpoch(timestamp);
    return dateTime.toString("yyyy-MM-dd hh:mm:ss");
}

QString UIManager::formatDuration(int64_t milliseconds) {
    int seconds = static_cast<int>(milliseconds / 1000);
    int minutes = seconds / 60;
    seconds %= 60;
    int hours = minutes / 60;
    minutes %= 60;

    if (hours > 0) {
        return QString("%1h %2m %3s").arg(hours).arg(minutes).arg(seconds);
    } else if (minutes > 0) {
        return QString("%1m %2s").arg(minutes).arg(seconds);
    } else {
        return QString("%1s").arg(seconds);
    }
}

QColor UIManager::getStatusColor(core::TransferStatus status) {
    switch (status) {
        case core::TransferStatus::Initializing:
            return QColor(100, 100, 255);  // Blue
        case core::TransferStatus::Waiting:
            return QColor(255, 215, 0);    // Gold
        case core::TransferStatus::InProgress:
            return QColor(0, 150, 255);    // Sky Blue
        case core::TransferStatus::Completed:
            return QColor(0, 180, 0);      // Green
        case core::TransferStatus::Failed:
            return QColor(255, 0, 0);      // Red
        case core::TransferStatus::Canceled:
            return QColor(255, 120, 0);    // Orange
        default:
            return QColor(128, 128, 128);  // Gray
    }
}

