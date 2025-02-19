// Copyright (c) 2011-2020 The Bitcoin Core developers
// Copyright (c) 2014-2024 The Reddcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/bitcoingui.h>

#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/createwalletdialog.h>
#include <qt/createwalletwizard.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/modaloverlay.h>
#include <qt/networkstyle.h>
#include <qt/notificator.h>
#include <qt/openuridialog.h>
#include <qt/optionsdialog.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/rpcconsole.h>
#include <qt/utilitydialog.h>

#ifdef ENABLE_WALLET
#include <qt/walletcontroller.h>
#include <qt/walletframe.h>
#include <qt/walletmodel.h>
#include <qt/walletview.h>
#endif // ENABLE_WALLET

#ifdef Q_OS_MAC
#include <qt/macdockiconhandler.h>
#endif

#include <functional>
#include <chain.h>
#include <chainparams.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <node/ui_interface.h>
#include <rpc/server.h>
#include <univalue.h>
#include <util/system.h>
#include <util/translation.h>
#include <validation.h>
#include <warnings.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QFile>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QProgressDialog>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolBar>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWindow>


const std::string BitcoinGUI::DEFAULT_UIPLATFORM =
#if defined(Q_OS_MAC)
        "macosx"
#elif defined(Q_OS_WIN)
        "windows"
#else
        "other"
#endif
        ;

BitcoinGUI::BitcoinGUI(interfaces::Node& node, const PlatformStyle *_platformStyle, const NetworkStyle *networkStyle, QWidget *parent) :
    QMainWindow(parent),
    m_node(node),
    trayIconMenu{new QMenu()},
    platformStyle(_platformStyle),
    m_network_style(networkStyle)
{
    QSettings settings;
    if (!restoreGeometry(settings.value("MainWindowGeometry").toByteArray())) {
        // Restore failed (perhaps missing setting), center the window
        move(QGuiApplication::primaryScreen()->availableGeometry().center() - frameGeometry().center());
    }

    setContextMenuPolicy(Qt::PreventContextMenu);

#ifdef ENABLE_WALLET
    enableWallet = WalletModel::isWalletEnabled();
#endif // ENABLE_WALLET
    QApplication::setWindowIcon(m_network_style->getTrayAndWindowIcon());
    setWindowIcon(m_network_style->getTrayAndWindowIcon());
    updateWindowTitle();

    rpcConsole = new RPCConsole(node, _platformStyle, nullptr);
    helpMessageDialog = new HelpMessageDialog(this, networkStyle, false, false);
#ifdef ENABLE_WALLET
    if(enableWallet)
    {
        /** Create wallet frame and make it the central widget */
        walletFrame = new WalletFrame(_platformStyle, this);
        connect(walletFrame, &WalletFrame::createWalletButtonClicked, [this] {
            auto activity = new CreateWalletWizardActivity(getWalletController(), this);
            connect(activity, &CreateWalletWizardActivity::finished, activity, &QObject::deleteLater);
            activity->create();
        });
        setCentralWidget(walletFrame);
    } else
#endif // ENABLE_WALLET
    {
        /* When compiled without wallet or -disablewallet is provided,
         * the central widget is the rpc console.
         */
        setCentralWidget(rpcConsole);
        Q_EMIT consoleShown(rpcConsole);
    }

    modalOverlay = new ModalOverlay(enableWallet, this->centralWidget());

    // Accept D&D of URIs
    setAcceptDrops(true);

    // Create actions for the toolbar, menu bar and tray/dock icon
    // Needs walletFrame to be initialized
    createActions();

    // Create application menu bar
    createMenuBar();

    // Create the toolbars
    createToolBars();

    // Create system tray icon and notification
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        createTrayIcon();
    }
    notificator = new Notificator(QApplication::applicationName(), trayIcon, this);

    // Create status bar
    statusBar();

    // Disable size grip because it looks ugly and nobody needs it
    statusBar()->setSizeGripEnabled(false);

    // Status bar notification icons
    QFrame *frameBlocks = new QFrame();
    frameBlocks->setContentsMargins(0,0,0,0);
    frameBlocks->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    QHBoxLayout *frameBlocksLayout = new QHBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(3,0,3,0);
    frameBlocksLayout->setSpacing(3);
    unitDisplayControl = new UnitDisplayStatusBarControl(platformStyle);
    labelWalletEncryptionIcon = new GUIUtil::ClickableLabel(platformStyle);
    walletstakingStatusControl = new GUIUtil::ClickableLabel(platformStyle);
    nodestakingStatusControl = new GUIUtil::ClickableLabel(platformStyle);
    labelWalletHDStatusIcon = new GUIUtil::ThemedLabel(platformStyle);
    labelProxyIcon = new GUIUtil::ClickableLabel(platformStyle);
    connectionsControl = new GUIUtil::ClickableLabel(platformStyle);
    labelBlocksIcon = new GUIUtil::ClickableLabel(platformStyle);
    if(enableWallet)
    {
        frameBlocksLayout->addStretch();
        frameBlocksLayout->addWidget(unitDisplayControl);
        frameBlocksLayout->addStretch();
        frameBlocksLayout->addWidget(labelWalletEncryptionIcon);
        frameBlocksLayout->addWidget(labelWalletHDStatusIcon);
        frameBlocksLayout->addStretch();
        frameBlocksLayout->addWidget(walletstakingStatusControl);
    }
    frameBlocksLayout->addWidget(labelProxyIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(connectionsControl);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    frameBlocksLayout->addStretch();
    if (enableWallet)
    {
        frameBlocksLayout->addWidget(nodestakingStatusControl);
        frameBlocksLayout->addStretch();
    }

    // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setVisible(false);
    progressBar = new GUIUtil::ProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    // Override style sheet for progress bar for styles that have a segmented progress bar,
    // as they make the text unreadable (workaround for issue #1071)
    // See https://doc.qt.io/qt-5/gallery.html
    QString curStyle = QApplication::style()->metaObject()->className();
    if(curStyle == "QWindowsStyle" || curStyle == "QWindowsXPStyle")
    {
        progressBar->setStyleSheet("QProgressBar { background-color: #e8e8e8; border: 1px solid grey; border-radius: 7px; padding: 1px; text-align: center; } QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #FF8000, stop: 1 orange); border-radius: 7px; margin: 0px; }");
    }

    // Check update label for update feedback download
    labelCheckUpdate = new GUIUtil::ClickableLabel(platformStyle);
    labelCheckUpdate->setVisible(false);

    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);
    statusBar()->addWidget(labelCheckUpdate);
    statusBar()->addPermanentWidget(frameBlocks);

    // Install event filter to be able to catch status tip events (QEvent::StatusTip)
    this->installEventFilter(this);

    // Initially wallet actions should be disabled
    setWalletActionsEnabled(false);

    // Subscribe to notifications from core
    subscribeToCoreSignals();

    connect(labelProxyIcon, &GUIUtil::ClickableLabel::clicked, [this] {
        openOptionsDialogWithTab(OptionsDialog::TAB_NETWORK);
    });

    connect(labelBlocksIcon, &GUIUtil::ClickableLabel::clicked, this, &BitcoinGUI::showModalOverlay);
    connect(progressBar, &GUIUtil::ClickableProgressBar::clicked, this, &BitcoinGUI::showModalOverlay);
    connect(labelCheckUpdate, &GUIUtil::ClickableLabel::clicked, this, &BitcoinGUI::showUpdatesClicked);

    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &BitcoinGUI::checkUpdates);
    timer->start(CHECK_UPDATE_DELAY);

    // check update on initial start
    checkUpdates();

#ifdef Q_OS_MAC
    m_app_nap_inhibitor = new CAppNapInhibitor;
#endif

    GUIUtil::handleCloseWindowShortcut(this);
}

BitcoinGUI::~BitcoinGUI()
{
    // Unsubscribe from notifications from core
    unsubscribeFromCoreSignals();

    QSettings settings;
    settings.setValue("MainWindowGeometry", saveGeometry());
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    delete m_app_nap_inhibitor;
    delete appMenuBar;
    MacDockIconHandler::cleanup();
#endif

    delete rpcConsole;
}

void BitcoinGUI::checkUpdates()
{
#ifdef ENABLE_WALLET

    QSettings settings;
    if (settings.value("bCheckGithub").toBool()) {
        // Get checkforupdatesinfo from rpc server
        UniValue result(UniValue::VOBJ);
        checkforupdatesinfo(result);

        std::string localversion = "";
        std::string remoteversion = "";
        bool updateavailable = false;
        std::string message = "";
        std::string warning = "";
        std::string officialDownloadLink = "";
        std::string errors = "";

        if (result.exists("localversion")) {
            localversion = result["localversion"].get_str();
        }
        if (result.exists("remoteversion")) {
            remoteversion = result["remoteversion"].get_str();
        }
        if (result.exists("updateavailable")) {
            updateavailable = result["updateavailable"].get_bool();
        }

        if (updateavailable) {
            labelCheckUpdate->setText(tr("Update to %1 is available.").arg(QString::fromStdString(localversion)));
            labelCheckUpdate->setVisible(true);
        } else {
            labelCheckUpdate->setVisible(false);
            labelCheckUpdate->setText(QString(""));
        }
    }
#endif
}

void BitcoinGUI::createActions()
{
    QActionGroup *tabGroup = new QActionGroup(this);
    connect(modalOverlay, &ModalOverlay::triggered, tabGroup, &QActionGroup::setEnabled);

    overviewAction = new QAction(platformStyle->SingleColorIcon(":/icons/overview"), tr("&Overview"), this);
    overviewAction->setStatusTip(tr("Show general overview of wallet"));
    overviewAction->setToolTip(overviewAction->statusTip());
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
    tabGroup->addAction(overviewAction);

    sendCoinsAction = new QAction(platformStyle->SingleColorIcon(":/icons/send"), tr("&Send"), this);
    sendCoinsAction->setStatusTip(tr("Send coins to a Reddcoin address"));
    sendCoinsAction->setToolTip(sendCoinsAction->statusTip());
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
    tabGroup->addAction(sendCoinsAction);

    sendCoinsMenuAction = new QAction(sendCoinsAction->text(), this);
    sendCoinsMenuAction->setStatusTip(sendCoinsAction->statusTip());
    sendCoinsMenuAction->setToolTip(sendCoinsMenuAction->statusTip());

    receiveCoinsAction = new QAction(platformStyle->SingleColorIcon(":/icons/receiving_addresses"), tr("&Receive"), this);
    receiveCoinsAction->setStatusTip(tr("Request payments (generates QR codes and reddcoin: URIs)"));
    receiveCoinsAction->setToolTip(receiveCoinsAction->statusTip());
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
    tabGroup->addAction(receiveCoinsAction);

    receiveCoinsMenuAction = new QAction(receiveCoinsAction->text(), this);
    receiveCoinsMenuAction->setStatusTip(receiveCoinsAction->statusTip());
    receiveCoinsMenuAction->setToolTip(receiveCoinsMenuAction->statusTip());

    historyAction = new QAction(platformStyle->SingleColorIcon(":/icons/history"), tr("&Transactions"), this);
    historyAction->setStatusTip(tr("Browse transaction history"));
    historyAction->setToolTip(historyAction->statusTip());
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
    tabGroup->addAction(historyAction);

    mintingAction = new QAction(platformStyle->SingleColorIcon(":/icons/staking"), tr("&Staking"), this);
    mintingAction->setStatusTip(tr("Show your staking capacity"));
    mintingAction->setToolTip(mintingAction->statusTip());
    mintingAction->setCheckable(true);
    mintingAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
    tabGroup->addAction(mintingAction);

#ifdef ENABLE_WALLET
    // These showNormalIfMinimized are needed because Send Coins and Receive Coins
    // can be triggered from the tray menu, and need to show the GUI to be useful.
    connect(overviewAction, &QAction::triggered, [this]{ showNormalIfMinimized(); });
    connect(overviewAction, &QAction::triggered, this, &BitcoinGUI::gotoOverviewPage);
    connect(sendCoinsAction, &QAction::triggered, [this]{ showNormalIfMinimized(); });
    connect(sendCoinsAction, &QAction::triggered, [this]{ gotoSendCoinsPage(); });
    connect(sendCoinsMenuAction, &QAction::triggered, [this]{ showNormalIfMinimized(); });
    connect(sendCoinsMenuAction, &QAction::triggered, [this]{ gotoSendCoinsPage(); });
    connect(receiveCoinsAction, &QAction::triggered, [this]{ showNormalIfMinimized(); });
    connect(receiveCoinsAction, &QAction::triggered, this, &BitcoinGUI::gotoReceiveCoinsPage);
    connect(receiveCoinsMenuAction, &QAction::triggered, [this]{ showNormalIfMinimized(); });
    connect(receiveCoinsMenuAction, &QAction::triggered, this, &BitcoinGUI::gotoReceiveCoinsPage);
    connect(historyAction, &QAction::triggered, [this]{ showNormalIfMinimized(); });
    connect(historyAction, &QAction::triggered, this, &BitcoinGUI::gotoHistoryPage);
    connect(mintingAction, &QAction::triggered, [this]{ showNormalIfMinimized(); });
    connect(mintingAction, &QAction::triggered, this, &BitcoinGUI::gotoMintingPage);
#endif // ENABLE_WALLET

    quitAction = new QAction(tr("E&xit"), this);
    quitAction->setStatusTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(tr("&About %1").arg(PACKAGE_NAME), this);
    aboutAction->setStatusTip(tr("Show information about %1").arg(PACKAGE_NAME));
    aboutAction->setMenuRole(QAction::AboutRole);
    aboutAction->setEnabled(false);
    aboutQtAction = new QAction(tr("About &Qt"), this);
    aboutQtAction->setStatusTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(tr("&Options…"), this);
    optionsAction->setStatusTip(tr("Modify configuration options for %1").arg(PACKAGE_NAME));
    optionsAction->setMenuRole(QAction::PreferencesRole);
    optionsAction->setEnabled(false);
    toggleHideAction = new QAction(tr("&Show / Hide"), this);
    toggleHideAction->setStatusTip(tr("Show or hide the main Window"));

    encryptWalletAction = new QAction(tr("&Encrypt Wallet…"), this);
    encryptWalletAction->setStatusTip(tr("Encrypt the private keys that belong to your wallet"));
    encryptWalletAction->setCheckable(true);
    backupWalletAction = new QAction(tr("&Backup Wallet…"), this);
    backupWalletAction->setStatusTip(tr("Backup wallet to another location"));
    changePassphraseAction = new QAction(tr("&Change Passphrase…"), this);
    changePassphraseAction->setStatusTip(tr("Change the passphrase used for wallet encryption"));
    unlockWalletAction = new QAction(tr("&Unlock Wallet"), this);
    unlockWalletAction->setStatusTip(tr("Unlock wallet"));
    lockWalletAction = new QAction(tr("&Lock Wallet"), this);
    lockWalletAction->setStatusTip(tr("Lock wallet"));
    enableStakingAction = new QAction(tr("&Enable Staking"), this);
    enableStakingAction->setStatusTip(tr("Enable wallet staking"));
    disableStakingAction = new QAction(tr("&Disable Staking"), this);
    disableStakingAction->setStatusTip(tr("Disable wallet staking"));
    signMessageAction = new QAction(tr("Sign &message…"), this);
    signMessageAction->setStatusTip(tr("Sign messages with your Reddcoin addresses to prove you own them"));
    verifyMessageAction = new QAction(tr("&Verify message…"), this);
    verifyMessageAction->setStatusTip(tr("Verify messages to ensure they were signed with specified Reddcoin addresses"));
    m_load_psbt_action = new QAction(tr("&Load PSBT from file…"), this);
    m_load_psbt_action->setStatusTip(tr("Load Partially Signed Reddcoin Transaction"));
    m_load_psbt_clipboard_action = new QAction(tr("Load PSBT from clipboard…"), this);
    m_load_psbt_clipboard_action->setStatusTip(tr("Load Partially Signed Reddcoin Transaction from clipboard"));

    openRPCConsoleAction = new QAction(tr("Node window"), this);
    openRPCConsoleAction->setStatusTip(tr("Open node debugging and diagnostic console"));
    // initially disable the debug window menu item
    openRPCConsoleAction->setEnabled(false);
    openRPCConsoleAction->setObjectName("openRPCConsoleAction");

    usedSendingAddressesAction = new QAction(tr("&Sending addresses"), this);
    usedSendingAddressesAction->setStatusTip(tr("Show the list of used sending addresses and labels"));
    usedReceivingAddressesAction = new QAction(tr("&Receiving addresses"), this);
    usedReceivingAddressesAction->setStatusTip(tr("Show the list of used receiving addresses and labels"));

    openAction = new QAction(tr("Open &URI…"), this);
    openAction->setStatusTip(tr("Open a reddcoin: URI"));

    m_open_wallet_action = new QAction(tr("Open Wallet"), this);
    m_open_wallet_action->setEnabled(false);
    m_open_wallet_action->setStatusTip(tr("Open a wallet"));
    m_open_wallet_menu = new QMenu(this);

    m_close_wallet_action = new QAction(tr("Close Wallet…"), this);
    m_close_wallet_action->setStatusTip(tr("Close wallet"));

    m_create_wallet_wiz_action = new QAction(tr("Create/ Restore Wallet…"), this);
    m_create_wallet_wiz_action->setEnabled(false);
    m_create_wallet_wiz_action->setStatusTip(tr("Create or restore a new HD wallet"));

    m_close_all_wallets_action = new QAction(tr("Close All Wallets…"), this);
    m_close_all_wallets_action->setStatusTip(tr("Close all wallets"));

    showHelpMessageAction = new QAction(tr("&Command-line options"), this);
    showHelpMessageAction->setMenuRole(QAction::NoRole);
    showHelpMessageAction->setStatusTip(tr("Show the %1 help message to get a list with possible Reddcoin command-line options").arg(PACKAGE_NAME));

    checkUpdatesAction = new QAction(tr("&Check for software updates"), this);
    checkUpdatesAction->setStatusTip(tr("Check for available %1 software updates").arg(PACKAGE_NAME));

    openWebSocialAction = new QAction(tr("Open Social Websites"), this);
    openWebSocialAction->setStatusTip(tr("Open Social Websites"));
    openWebSocialMenu = new QMenu(this);

    openWebReddcoinAction = new QAction(tr("&Website - reddcoin.com"), this);
    openWebReddcoinAction->setStatusTip(tr("Open the Reddcoin website in a web browser."));

    openWebReddloveAction = new QAction(tr("&Website - redd.love"), this);
    openWebReddloveAction->setStatusTip(tr("Open the Redd Love website in a web browser."));

    openWebWikiAction = new QAction(tr("&Website - Reddcoin Wiki"), this);
    openWebWikiAction->setStatusTip(tr("Open the Reddcoin Wiki website in a web browser."));

    openChatroomAction = new QAction(tr("&Chatroom - Discord"), this);
    openChatroomAction->setStatusTip(tr("Open the Reddcoin Discord chat in a web browser."));

    openForumAction = new QAction(tr("&Forum"), this);
    openForumAction->setStatusTip(tr("Open reddcointalk.org in a web browser."));

    m_mask_values_action = new QAction(tr("&Mask values"), this);
    m_mask_values_action->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_M));
    m_mask_values_action->setStatusTip(tr("Mask the values in the Overview tab"));
    m_mask_values_action->setCheckable(true);

    connect(quitAction, &QAction::triggered, qApp, QApplication::quit);
    connect(checkUpdatesAction, &QAction::triggered, this, &BitcoinGUI::showUpdatesClicked);
    connect(aboutAction, &QAction::triggered, this, &BitcoinGUI::aboutClicked);
    connect(aboutQtAction, &QAction::triggered, qApp, QApplication::aboutQt);
    connect(optionsAction, &QAction::triggered, this, &BitcoinGUI::optionsClicked);
    connect(toggleHideAction, &QAction::triggered, this, &BitcoinGUI::toggleHidden);
    connect(showHelpMessageAction, &QAction::triggered, this, &BitcoinGUI::showHelpMessageClicked);
    connect(openRPCConsoleAction, &QAction::triggered, this, &BitcoinGUI::showDebugWindow);
    // prevents an open debug window from becoming stuck/unusable on client shutdown
    connect(quitAction, &QAction::triggered, rpcConsole, &QWidget::hide);

    connect(openWebReddcoinAction, &QAction::triggered, this, &BitcoinGUI::openWebReddcoin);
    connect(openWebReddloveAction, &QAction::triggered, this, &BitcoinGUI::openWebReddlove);
    connect(openWebWikiAction, &QAction::triggered, this, &BitcoinGUI::openWebWiki);
    connect(openChatroomAction, &QAction::triggered, this, &BitcoinGUI::openChatroom);
    connect(openForumAction, &QAction::triggered, this, &BitcoinGUI::openForum);

#ifdef ENABLE_WALLET
    if(walletFrame)
    {
        connect(encryptWalletAction, &QAction::triggered, walletFrame, &WalletFrame::encryptWallet);
        connect(backupWalletAction, &QAction::triggered, walletFrame, &WalletFrame::backupWallet);
        connect(changePassphraseAction, &QAction::triggered, walletFrame, &WalletFrame::changePassphrase);
        connect(unlockWalletAction, &QAction::triggered, walletFrame, &WalletFrame::unlockWallet);
        connect(lockWalletAction, &QAction::triggered, [this]{walletFrame->lockWallet(true); });
        connect(enableStakingAction, &QAction::triggered, [this]{ walletFrame->enableStaking(true); });
        connect(disableStakingAction, &QAction::triggered, [this]{ walletFrame->enableStaking(false); });

        connect(signMessageAction, &QAction::triggered, [this]{ showNormalIfMinimized(); });
        connect(signMessageAction, &QAction::triggered, [this]{ gotoSignMessageTab(); });
        connect(m_load_psbt_action, &QAction::triggered, [this]{ gotoLoadPSBT(); });
        connect(m_load_psbt_clipboard_action, &QAction::triggered, [this]{ gotoLoadPSBT(true); });
        connect(verifyMessageAction, &QAction::triggered, [this]{ showNormalIfMinimized(); });
        connect(verifyMessageAction, &QAction::triggered, [this]{ gotoVerifyMessageTab(); });
        connect(usedSendingAddressesAction, &QAction::triggered, walletFrame, &WalletFrame::usedSendingAddresses);
        connect(usedReceivingAddressesAction, &QAction::triggered, walletFrame, &WalletFrame::usedReceivingAddresses);
        connect(openAction, &QAction::triggered, this, &BitcoinGUI::openClicked);
        connect(m_open_wallet_menu, &QMenu::aboutToShow, [this] {
            m_open_wallet_menu->clear();
            for (const std::pair<const std::string, bool>& i : m_wallet_controller->listWalletDir()) {
                const std::string& path = i.first;
                QString name = path.empty() ? QString("["+tr("default wallet")+"]") : QString::fromStdString(path);
                // Menu items remove single &. Single & are shown when && is in
                // the string, but only the first occurrence. So replace only
                // the first & with &&.
                name.replace(name.indexOf(QChar('&')), 1, QString("&&"));
                QAction* action = m_open_wallet_menu->addAction(name);

                if (i.second) {
                    // This wallet is already loaded
                    action->setEnabled(false);
                    continue;
                }

                connect(action, &QAction::triggered, [this, path] {
                    auto activity = new OpenWalletActivity(m_wallet_controller, this);
                    connect(activity, &OpenWalletActivity::opened, this, &BitcoinGUI::setCurrentWallet);
                    connect(activity, &OpenWalletActivity::finished, activity, &QObject::deleteLater);
                    activity->open(path);
                });
            }
            if (m_open_wallet_menu->isEmpty()) {
                QAction* action = m_open_wallet_menu->addAction(tr("No wallets available"));
                action->setEnabled(false);
            }
        });
        connect(m_close_wallet_action, &QAction::triggered, [this] {
            m_wallet_controller->closeWallet(walletFrame->currentWalletModel(), this);
        });

	connect(m_create_wallet_wiz_action, &QAction::triggered, [this] {
	    auto activity = new CreateWalletWizardActivity(m_wallet_controller, this);
	    connect(activity, &CreateWalletWizardActivity::created, this, &BitcoinGUI::setCurrentWallet);
	    connect(activity, &CreateWalletWizardActivity::finished, activity, &QObject::deleteLater);
	    activity->create();
	});
        connect(m_close_all_wallets_action, &QAction::triggered, [this] {
            m_wallet_controller->closeAllWallets(this);
        });
        connect(m_mask_values_action, &QAction::toggled, this, &BitcoinGUI::setPrivacy);
    }
#endif // ENABLE_WALLET

    connect(new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_C), this), &QShortcut::activated, this, &BitcoinGUI::showDebugWindowActivateConsole);
    connect(new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_D), this), &QShortcut::activated, this, &BitcoinGUI::showDebugWindow);
}

void BitcoinGUI::createMenuBar()
{
#ifdef Q_OS_MAC
    // Create a decoupled menu bar on Mac which stays even if the window is closed
    appMenuBar = new QMenuBar();
#else
    // Get the main window's menu bar on other platforms
    appMenuBar = menuBar();
#endif

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    if(walletFrame)
    {
        file->addAction(m_create_wallet_wiz_action);
        file->addAction(m_open_wallet_action);
        file->addAction(m_close_wallet_action);
        file->addAction(m_close_all_wallets_action);
        file->addSeparator();
        file->addAction(openAction);
        file->addAction(backupWalletAction);
        file->addAction(signMessageAction);
        file->addAction(verifyMessageAction);
        file->addAction(m_load_psbt_action);
        file->addAction(m_load_psbt_clipboard_action);
        file->addSeparator();
    }
    file->addAction(quitAction);

    QMenu *settings = appMenuBar->addMenu(tr("&Settings"));
    if(walletFrame)
    {
        settings->addAction(encryptWalletAction);
        settings->addAction(changePassphraseAction);
        settings->addAction(unlockWalletAction);
        settings->addAction(lockWalletAction);
        settings->addSeparator();
        settings->addAction(enableStakingAction);
        settings->addAction(disableStakingAction);
        settings->addSeparator();
        settings->addAction(m_mask_values_action);
        settings->addSeparator();
    }
    settings->addAction(optionsAction);

    QMenu* window_menu = appMenuBar->addMenu(tr("&Window"));

    QAction* minimize_action = window_menu->addAction(tr("Minimize"));
    minimize_action->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_M));
    connect(minimize_action, &QAction::triggered, [] {
        QApplication::activeWindow()->showMinimized();
    });
    connect(qApp, &QApplication::focusWindowChanged, this, [minimize_action] (QWindow* window) {
        minimize_action->setEnabled(window != nullptr && (window->flags() & Qt::Dialog) != Qt::Dialog && window->windowState() != Qt::WindowMinimized);
    });

#ifdef Q_OS_MAC
    QAction* zoom_action = window_menu->addAction(tr("Zoom"));
    connect(zoom_action, &QAction::triggered, [] {
        QWindow* window = qApp->focusWindow();
        if (window->windowState() != Qt::WindowMaximized) {
            window->showMaximized();
        } else {
            window->showNormal();
        }
    });

    connect(qApp, &QApplication::focusWindowChanged, this, [zoom_action] (QWindow* window) {
        zoom_action->setEnabled(window != nullptr);
    });
#endif

    if (walletFrame) {
#ifdef Q_OS_MAC
        window_menu->addSeparator();
        QAction* main_window_action = window_menu->addAction(tr("Main Window"));
        connect(main_window_action, &QAction::triggered, [this] {
            GUIUtil::bringToFront(this);
        });
#endif
        window_menu->addSeparator();
        window_menu->addAction(usedSendingAddressesAction);
        window_menu->addAction(usedReceivingAddressesAction);
    }

    window_menu->addSeparator();
    for (RPCConsole::TabTypes tab_type : rpcConsole->tabs()) {
        QAction* tab_action = window_menu->addAction(rpcConsole->tabTitle(tab_type));
        tab_action->setShortcut(rpcConsole->tabShortcut(tab_type));
        connect(tab_action, &QAction::triggered, [this, tab_type] {
            rpcConsole->setTabFocus(tab_type);
            showDebugWindow();
        });
    }

    QMenu *help = appMenuBar->addMenu(tr("&Help"));

    openWebSocialMenu->addAction(openWebReddcoinAction);
    openWebSocialMenu->addAction(openWebReddloveAction);
    openWebSocialMenu->addAction(openWebWikiAction);
    openWebSocialMenu->addAction(openChatroomAction);
    openWebSocialMenu->addAction(openForumAction);
    openWebSocialAction->setMenu(openWebSocialMenu);

    help->addAction(showHelpMessageAction);
    help->addAction(checkUpdatesAction);
    help->addSeparator();
    help->addAction(openWebSocialAction);
    help->addSeparator();
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);
}

void BitcoinGUI::createToolBars()
{
    if (walletFrame) {
        // add a label containing the merged AppIcon and Name as the first element on toolbar
        imageLogo = new QLabel();
        imageLogo->setPixmap(createLogo());
        imageLogo->setObjectName("logo");
        imageLogo->setMaximumWidth(100);

        QToolBar *toolbar = addToolBar(tr("Tabs toolbar"));
        appToolBar = toolbar;
        toolbar->addWidget(imageLogo);
        toolbar->setMovable(false);
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toolbar->addAction(overviewAction);
        toolbar->addAction(sendCoinsAction);
        toolbar->addAction(receiveCoinsAction);
        toolbar->addAction(historyAction);
        toolbar->addAction(mintingAction);
        overviewAction->setChecked(true);

#ifdef ENABLE_WALLET
        QWidget *spacer = new QWidget();
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        toolbar->addWidget(spacer);

        m_wallet_selector = new QComboBox();
        m_wallet_selector->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        connect(m_wallet_selector, qOverload<int>(&QComboBox::currentIndexChanged), this, &BitcoinGUI::setCurrentWalletBySelectorIndex);

        m_wallet_selector_label = new QLabel();
        m_wallet_selector_label->setText(tr("Wallet:") + " ");
        m_wallet_selector_label->setBuddy(m_wallet_selector);

        m_wallet_selector_label_action = appToolBar->addWidget(m_wallet_selector_label);
        m_wallet_selector_action = appToolBar->addWidget(m_wallet_selector);

        m_wallet_selector_label_action->setVisible(false);
        m_wallet_selector_action->setVisible(false);
#endif
    }
}

void BitcoinGUI::setClientModel(ClientModel *_clientModel, interfaces::BlockAndHeaderTipInfo* tip_info)
{
    this->clientModel = _clientModel;
    if(_clientModel)
    {
        // Create system tray menu (or setup the dock menu) that late to prevent users from calling actions,
        // while the client has not yet fully loaded
        createTrayIconMenu();

        // Keep up to date with client
        setNetworkActive(m_node.getNetworkActive());
        connect(connectionsControl, &GUIUtil::ClickableLabel::clicked, [this] {
            GUIUtil::PopupMenu(m_network_context_menu, QCursor::pos());
        });
        connect(_clientModel, &ClientModel::numConnectionsChanged, this, &BitcoinGUI::setNumConnections);
        connect(_clientModel, &ClientModel::networkActiveChanged, this, &BitcoinGUI::setNetworkActive);
        modalOverlay->setKnownBestHeight(tip_info->header_height, QDateTime::fromTime_t(tip_info->header_time));
        setNumBlocks(tip_info->block_height, QDateTime::fromTime_t(tip_info->block_time), tip_info->verification_progress, false, SynchronizationState::INIT_DOWNLOAD);
        connect(_clientModel, &ClientModel::numBlocksChanged, this, &BitcoinGUI::setNumBlocks);

        // Receive and report messages from client model
        connect(_clientModel, &ClientModel::message, [this](const QString &title, const QString &message, unsigned int style){
            this->message(title, message, style);
        });

        // Show progress dialog
        connect(_clientModel, &ClientModel::showProgress, this, &BitcoinGUI::showProgress);

        rpcConsole->setClientModel(_clientModel, tip_info->block_height, tip_info->block_time, tip_info->verification_progress);

        updateProxyIcon();

#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->setClientModel(_clientModel);
        }
        connect(labelWalletEncryptionIcon, &GUIUtil::ClickableLabel::clicked, [this] {
                    GUIUtil::PopupMenu(m_lock_context_menu, QCursor::pos());
                });
        connect(walletstakingStatusControl, &GUIUtil::ClickableLabel::clicked, [this] {
                    GUIUtil::PopupMenu(m_wallet_staking_context_menu, QCursor::pos());
                });
        setNodeStakingActive(m_node.getNodeStakingActive());
        connect(nodestakingStatusControl, &GUIUtil::ClickableLabel::clicked, [this] {
                    GUIUtil::PopupMenu(m_node_staking_context_menu, QCursor::pos());
                });
        updateNodeStakingStatus();
        connect(_clientModel, &ClientModel::nodeStakingActiveChanged, this, &BitcoinGUI::setNodeStakingActive);
        connect(_clientModel, &ClientModel::walletStakingActiveChanged, this, &BitcoinGUI::updateWalletStakingStatus);
#endif // ENABLE_WALLET
        unitDisplayControl->setOptionsModel(_clientModel->getOptionsModel());

        OptionsModel* optionsModel = _clientModel->getOptionsModel();
        if (optionsModel && trayIcon) {
            // be aware of the tray icon disable state change reported by the OptionsModel object.
            connect(optionsModel, &OptionsModel::showTrayIconChanged, trayIcon, &QSystemTrayIcon::setVisible);

            // initialize the disable state of the tray icon with the current value in the model.
            trayIcon->setVisible(optionsModel->getShowTrayIcon());
        }
        connect(optionsModel, &OptionsModel::uiStyleChanged, this, &BitcoinGUI::updateStyle);
        updateStyle(optionsModel->getStyle());
        connect(optionsModel, &OptionsModel::uiThemeChanged, this, &BitcoinGUI::updateTheme);
        updateTheme(optionsModel->getTheme());
    } else {
        // Disable possibility to show main window via action
        toggleHideAction->setEnabled(false);
        if(trayIconMenu)
        {
            // Disable context menu on tray icon
            trayIconMenu->clear();
        }
        // Propagate cleared model to child objects
        rpcConsole->setClientModel(nullptr);
#ifdef ENABLE_WALLET
        if (walletFrame)
        {
            walletFrame->setClientModel(nullptr);
        }
#endif // ENABLE_WALLET
        unitDisplayControl->setOptionsModel(nullptr);
    }
}

#ifdef ENABLE_WALLET
void BitcoinGUI::setWalletController(WalletController* wallet_controller)
{
    assert(!m_wallet_controller);
    assert(wallet_controller);

    m_wallet_controller = wallet_controller;

    m_create_wallet_wiz_action->setEnabled(true);
    m_open_wallet_action->setEnabled(true);
    m_open_wallet_action->setMenu(m_open_wallet_menu);

    GUIUtil::ExceptionSafeConnect(wallet_controller, &WalletController::walletAdded, this, &BitcoinGUI::addWallet);
    connect(wallet_controller, &WalletController::walletRemoved, this, &BitcoinGUI::removeWallet);

    for (WalletModel* wallet_model : m_wallet_controller->getOpenWallets()) {
        addWallet(wallet_model);
    }
}

WalletController* BitcoinGUI::getWalletController()
{
    return m_wallet_controller;
}

void BitcoinGUI::addWallet(WalletModel* walletModel)
{
    if (!walletFrame) return;

    WalletView* wallet_view = new WalletView(platformStyle, walletFrame);
    if (!walletFrame->addWallet(walletModel, wallet_view)) return;

    rpcConsole->addWallet(walletModel);
    if (m_wallet_selector->count() == 0) {
        setWalletActionsEnabled(true);
    } else if (m_wallet_selector->count() == 1) {
        m_wallet_selector_label_action->setVisible(true);
        m_wallet_selector_action->setVisible(true);
    }

    connect(wallet_view, &WalletView::outOfSyncWarningClicked, this, &BitcoinGUI::showModalOverlay);
    connect(wallet_view, &WalletView::transactionClicked, this, &BitcoinGUI::gotoHistoryPage);
    connect(wallet_view, &WalletView::coinsSent, this, &BitcoinGUI::gotoHistoryPage);
    connect(wallet_view, &WalletView::message, [this](const QString& title, const QString& message, unsigned int style) {
        this->message(title, message, style);
    });
    connect(wallet_view, &WalletView::encryptionStatusChanged, this, &BitcoinGUI::updateWalletStatus);
    connect(wallet_view, &WalletView::stakingActiveChanged, this, &BitcoinGUI::setWalletStakingActive);
    connect(wallet_view, &WalletView::stakingStatusChanged, this, &BitcoinGUI::updateWalletStakingStatus);
    connect(wallet_view, &WalletView::incomingTransaction, this, &BitcoinGUI::incomingTransaction);
    connect(wallet_view, &WalletView::hdEnabledStatusChanged, this, &BitcoinGUI::updateWalletStatus);
    connect(this, &BitcoinGUI::setPrivacy, wallet_view, &WalletView::setPrivacy);
    wallet_view->setPrivacy(isPrivacyModeActivated());
    const QString display_name = walletModel->getDisplayName();
    m_wallet_selector->addItem(display_name, QVariant::fromValue(walletModel));
}

void BitcoinGUI::removeWallet(WalletModel* walletModel)
{
    if (!walletFrame) return;

    labelWalletHDStatusIcon->hide();
    labelWalletEncryptionIcon->hide();
    walletstakingStatusControl->hide();

    int index = m_wallet_selector->findData(QVariant::fromValue(walletModel));
    m_wallet_selector->removeItem(index);
    if (m_wallet_selector->count() == 0) {
        setWalletActionsEnabled(false);
        overviewAction->setChecked(true);
    } else if (m_wallet_selector->count() == 1) {
        m_wallet_selector_label_action->setVisible(false);
        m_wallet_selector_action->setVisible(false);
    }
    rpcConsole->removeWallet(walletModel);
    walletFrame->removeWallet(walletModel);
    updateWindowTitle();
}

void BitcoinGUI::setCurrentWallet(WalletModel* wallet_model)
{
    if (!walletFrame) return;
    walletFrame->setCurrentWallet(wallet_model);
    for (int index = 0; index < m_wallet_selector->count(); ++index) {
        if (m_wallet_selector->itemData(index).value<WalletModel*>() == wallet_model) {
            m_wallet_selector->setCurrentIndex(index);
            break;
        }
    }
    updateWindowTitle();
}

void BitcoinGUI::setCurrentWalletBySelectorIndex(int index)
{
    WalletModel* wallet_model = m_wallet_selector->itemData(index).value<WalletModel*>();
    if (wallet_model) setCurrentWallet(wallet_model);
}

void BitcoinGUI::removeAllWallets()
{
    if(!walletFrame)
        return;
    setWalletActionsEnabled(false);
    walletFrame->removeAllWallets();
}
#endif // ENABLE_WALLET

void BitcoinGUI::setWalletActionsEnabled(bool enabled)
{
    overviewAction->setEnabled(enabled);
    sendCoinsAction->setEnabled(enabled);
    sendCoinsMenuAction->setEnabled(enabled);
    receiveCoinsAction->setEnabled(enabled);
    receiveCoinsMenuAction->setEnabled(enabled);
    historyAction->setEnabled(enabled);
    mintingAction->setEnabled(enabled);
    encryptWalletAction->setEnabled(enabled);
    backupWalletAction->setEnabled(enabled);
    changePassphraseAction->setEnabled(enabled);
    unlockWalletAction->setEnabled(enabled);
    lockWalletAction->setEnabled(enabled);
    enableStakingAction->setEnabled(enabled);
    disableStakingAction->setEnabled(enabled);
    signMessageAction->setEnabled(enabled);
    verifyMessageAction->setEnabled(enabled);
    usedSendingAddressesAction->setEnabled(enabled);
    usedReceivingAddressesAction->setEnabled(enabled);
    openAction->setEnabled(enabled);
    m_close_wallet_action->setEnabled(enabled);
    m_close_all_wallets_action->setEnabled(enabled);
}

QPixmap BitcoinGUI::createLogo()
{
    // add a label containing the merged AppIcon and Name as first element on toolbar
    const QSize toolbarIconSize(120, 32);
    QPixmap logo(toolbarIconSize);
    logo.fill(Qt::transparent);

    QPainter pixPaint(&logo);

    QImage logoname(platformStyle->SingleColorImage(QStringLiteral(":/images/logo")));
    logoname = logoname.scaled(QSize(54, 32), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // draw the bitcoin icon, expected size of PNG: 1024x1024
    QRect rectIcon1(QPoint(0, 0), QSize(32, 32));
    // add the name icon following
    QRect rectIcon2(QPoint(33, 0), QSize(54, 32));

    pixPaint.drawPixmap(rectIcon1, m_network_style->getAppIcon().pixmap(QSize(32, 32)));
    pixPaint.drawImage(rectIcon2, logoname);

    return logo;
}

void BitcoinGUI::createTrayIcon()
{
    assert(QSystemTrayIcon::isSystemTrayAvailable());

#ifndef Q_OS_MAC
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        trayIcon = new QSystemTrayIcon(m_network_style->getTrayAndWindowIcon(), this);
        QString toolTip = tr("%1 client").arg(PACKAGE_NAME) + " " + m_network_style->getTitleAddText();
        trayIcon->setToolTip(toolTip);
    }
#endif
}

void BitcoinGUI::createTrayIconMenu()
{
#ifndef Q_OS_MAC
    // return if trayIcon is unset (only on non-macOSes)
    if (!trayIcon)
        return;

    trayIcon->setContextMenu(trayIconMenu.get());
    connect(trayIcon, &QSystemTrayIcon::activated, this, &BitcoinGUI::trayIconActivated);
#else
    // Note: On macOS, the Dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    connect(dockIconHandler, &MacDockIconHandler::dockIconClicked, this, &BitcoinGUI::macosDockIconActivated);
    trayIconMenu->setAsDockMenu();
#endif

    // Configuration of the tray icon (or Dock icon) menu
#ifndef Q_OS_MAC
    // Note: On macOS, the Dock icon's menu already has Show / Hide action.
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
#endif
    if (enableWallet) {
        trayIconMenu->addAction(sendCoinsMenuAction);
        trayIconMenu->addAction(receiveCoinsMenuAction);
        trayIconMenu->addSeparator();
        trayIconMenu->addAction(signMessageAction);
        trayIconMenu->addAction(verifyMessageAction);
        trayIconMenu->addSeparator();
    }
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openRPCConsoleAction);
#ifndef Q_OS_MAC // This is built-in on macOS
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif
}

#ifndef Q_OS_MAC
void BitcoinGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHidden();
    }
}
#else
void BitcoinGUI::macosDockIconActivated()
{
    show();
    activateWindow();
}
#endif

void BitcoinGUI::optionsClicked()
{
    openOptionsDialogWithTab(OptionsDialog::TAB_MAIN);
}

void BitcoinGUI::aboutClicked()
{
    if(!clientModel)
        return;

    HelpMessageDialog dlg(this, m_network_style, true, false);
    dlg.exec();
}

void BitcoinGUI::showUpdatesClicked()
{
    if(!clientModel)
        return;

    HelpMessageDialog dlg(this, m_network_style, false, true);
    dlg.exec();
}

void BitcoinGUI::showDebugWindow()
{
    GUIUtil::bringToFront(rpcConsole);
    Q_EMIT consoleShown(rpcConsole);
}

void BitcoinGUI::showDebugWindowActivateConsole()
{
    rpcConsole->setTabFocus(RPCConsole::TabTypes::CONSOLE);
    showDebugWindow();
}

void BitcoinGUI::showHelpMessageClicked()
{
    GUIUtil::bringToFront(helpMessageDialog);
}

#ifdef ENABLE_WALLET
void BitcoinGUI::openClicked()
{
    OpenURIDialog dlg(this);
    if(dlg.exec())
    {
        Q_EMIT receivedURI(dlg.getURI());
    }
}

void BitcoinGUI::gotoOverviewPage()
{
    overviewAction->setChecked(true);
    if (walletFrame) walletFrame->gotoOverviewPage();
}

void BitcoinGUI::gotoHistoryPage()
{
    historyAction->setChecked(true);
    if (walletFrame) walletFrame->gotoHistoryPage();
}

void BitcoinGUI::gotoMintingPage()
{
    if (walletFrame) walletFrame->gotoMintingPage();
}

void BitcoinGUI::gotoReceiveCoinsPage()
{
    receiveCoinsAction->setChecked(true);
    if (walletFrame) walletFrame->gotoReceiveCoinsPage();
}

void BitcoinGUI::gotoSendCoinsPage(QString addr)
{
    sendCoinsAction->setChecked(true);
    if (walletFrame) walletFrame->gotoSendCoinsPage(addr);
}

void BitcoinGUI::gotoSignMessageTab(QString addr)
{
    if (walletFrame) walletFrame->gotoSignMessageTab(addr);
}

void BitcoinGUI::gotoVerifyMessageTab(QString addr)
{
    if (walletFrame) walletFrame->gotoVerifyMessageTab(addr);
}
void BitcoinGUI::gotoLoadPSBT(bool from_clipboard)
{
    if (walletFrame) walletFrame->gotoLoadPSBT(from_clipboard);
}

void BitcoinGUI::openWebReddcoin() {
    QDesktopServices::openUrl(QUrl("https://reddcoin.com"));
}

void BitcoinGUI::openWebReddlove() {
    QDesktopServices::openUrl(QUrl("https://redd.love"));
}

void BitcoinGUI::openWebWiki() {
    QDesktopServices::openUrl(QUrl("https://wiki.reddcoin.com"));
}

void BitcoinGUI::openChatroom() {
    QDesktopServices::openUrl(QUrl("https://discord.gg/ZHbzsz56V5"));
}

void BitcoinGUI::openForum() {
    QDesktopServices::openUrl(QUrl("https://reddcointalk.org/"));
}
#endif // ENABLE_WALLET

void BitcoinGUI::updateNetworkState()
{
    int count = clientModel->getNumConnections();
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: case 2: case 3: icon = ":/icons/connect_1"; break;
    case 4: case 5: case 6: icon = ":/icons/connect_2"; break;
    case 7: case 8: case 9: icon = ":/icons/connect_3"; break;
    default: icon = ":/icons/connect_4"; break;
    }

    QString tooltip;

    if (m_node.getNetworkActive()) {
        //: A substring of the tooltip.
        tooltip = tr("%n active connection(s) to Reddcoin network.", "", count);
    } else {
        //: A substring of the tooltip.
        tooltip = tr("Network activity disabled.");
        icon = ":/icons/network_disabled";
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QLatin1String("<nobr>") + tooltip + QLatin1String("<br>") +
              //: A substring of the tooltip. "More actions" are available via the context menu.
              tr("Click for more actions.") + QLatin1String("</nobr>");
    connectionsControl->setToolTip(tooltip);

    connectionsControl->setThemedPixmap(icon, STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE);
}

void BitcoinGUI::setNumConnections(int count)
{
    updateNetworkState();
}

void BitcoinGUI::setNetworkActive(bool network_active)
{
    updateNetworkState();
    m_network_context_menu->clear();
    m_network_context_menu->addAction(
        //: A context menu item. The "Peers tab" is an element of the "Node window".
        tr("Show Peers tab"),
        [this] {
            rpcConsole->setTabFocus(RPCConsole::TabTypes::PEERS);
            showDebugWindow();
        });
    m_network_context_menu->addAction(
        network_active ?
            //: A context menu item.
            tr("Disable network activity") :
            //: A context menu item. The network activity was disabled previously.
            tr("Enable network activity"),
        [this, new_state = !network_active] { m_node.setNetworkActive(new_state); });
}


void BitcoinGUI::updateHeadersSyncProgressLabel()
{
    int64_t headersTipTime = clientModel->getHeaderTipTime();
    int headersTipHeight = clientModel->getHeaderTipHeight();
    int estHeadersLeft = (GetTime() - headersTipTime) / Params().GetConsensus().nPowTargetSpacing;
    if (estHeadersLeft > HEADER_HEIGHT_DELTA_SYNC)
        progressBarLabel->setText(tr("Syncing Headers (%1%)…").arg(QString::number(100.0 / (headersTipHeight+estHeadersLeft)*headersTipHeight, 'f', 1)));
}

void BitcoinGUI::openOptionsDialogWithTab(OptionsDialog::Tab tab)
{
    if (!clientModel || !clientModel->getOptionsModel())
        return;

    OptionsDialog dlg(this, enableWallet);
    dlg.setCurrentTab(tab);
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void BitcoinGUI::setNumBlocks(int count, const QDateTime& blockDate, double nVerificationProgress, bool header, SynchronizationState sync_state)
{
// Disabling macOS App Nap on initial sync, disk and reindex operations.
#ifdef Q_OS_MAC
    if (sync_state == SynchronizationState::POST_INIT) {
        m_app_nap_inhibitor->enableAppNap();
    } else {
        m_app_nap_inhibitor->disableAppNap();
    }
#endif

    if (modalOverlay)
    {
        if (header)
            modalOverlay->setKnownBestHeight(count, blockDate);
        else
            modalOverlay->tipUpdate(count, blockDate, nVerificationProgress);
    }
    if (!clientModel)
        return;

    // Prevent orphan statusbar messages (e.g. hover Quit in main menu, wait until chain-sync starts -> garbled text)
    statusBar()->clearMessage();

    // Acquire current block source
    enum BlockSource blockSource = clientModel->getBlockSource();
    switch (blockSource) {
        case BlockSource::NETWORK:
            if (header) {
                updateHeadersSyncProgressLabel();
                return;
            }
            progressBarLabel->setText(tr("Synchronizing with network…"));
            updateHeadersSyncProgressLabel();
            break;
        case BlockSource::DISK:
            if (header) {
                progressBarLabel->setText(tr("Indexing blocks on disk…"));
            } else {
                progressBarLabel->setText(tr("Processing blocks on disk…"));
            }
            break;
        case BlockSource::REINDEX:
            progressBarLabel->setText(tr("Reindexing blocks on disk…"));
            break;
        case BlockSource::NONE:
            if (header) {
                return;
            }
            progressBarLabel->setText(tr("Connecting to peers…"));
            break;
    }

    QString tooltip;

    QDateTime currentDate = QDateTime::currentDateTime();
    qint64 secs = blockDate.secsTo(currentDate);

    tooltip = tr("Processed %n block(s) of transaction history.", "", count);

    // Set icon state: spinning if catching up, tick otherwise
    if (secs < MAX_BLOCK_TIME_GAP) {
        tooltip = tr("Up to date") + QString(".<br>") + tooltip;
        labelBlocksIcon->setThemedPixmap(QStringLiteral(":/icons/synced"), STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE);

#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->showOutOfSyncWarning(false);
            modalOverlay->showHide(true, true);
        }
#endif // ENABLE_WALLET

        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);
    }
    else
    {
        QString timeBehindText = GUIUtil::formatNiceTimeOffset(secs);

        progressBarLabel->setVisible(true);
        progressBar->setFormat(tr("%1 behind").arg(timeBehindText));
        progressBar->setMaximum(1000000000);
        progressBar->setValue(nVerificationProgress * 1000000000.0 + 0.5);
        progressBar->setVisible(true);

        tooltip = tr("Catching up…") + QString("<br>") + tooltip;
        if(count != prevBlocks)
        {
            labelBlocksIcon->setThemedPixmap(
                QString(":/animation/spinner-%1").arg(spinnerFrame, 3, 10, QChar('0')),
                STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE);
            spinnerFrame = (spinnerFrame + 1) % SPINNER_FRAMES;
        }
        prevBlocks = count;

#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->showOutOfSyncWarning(true);
            modalOverlay->showHide();
        }
#endif // ENABLE_WALLET

        tooltip += QString("<br>");
        tooltip += tr("Last received block was generated %1 ago.").arg(timeBehindText);
        tooltip += QString("<br>");
        tooltip += tr("Transactions after this will not yet be visible.");
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);

#ifdef ENABLE_WALLET
    updateWalletStakingStatus();
#endif // ENABLE_WALLET
}

void BitcoinGUI::message(const QString& title, QString message, unsigned int style, bool* ret, const QString& detailed_message)
{
    // Default title. On macOS, the window title is ignored (as required by the macOS Guidelines).
    QString strTitle{PACKAGE_NAME};
    // Default to information icon
    int nMBoxIcon = QMessageBox::Information;
    int nNotifyIcon = Notificator::Information;

    QString msgType;
    if (!title.isEmpty()) {
        msgType = title;
    } else {
        switch (style) {
        case CClientUIInterface::MSG_ERROR:
            msgType = tr("Error");
            message = tr("Error: %1").arg(message);
            break;
        case CClientUIInterface::MSG_WARNING:
            msgType = tr("Warning");
            message = tr("Warning: %1").arg(message);
            break;
        case CClientUIInterface::MSG_INFORMATION:
            msgType = tr("Information");
            // No need to prepend the prefix here.
            break;
        default:
            break;
        }
    }

    if (!msgType.isEmpty()) {
        strTitle += " - " + msgType;
    }

    if (style & CClientUIInterface::ICON_ERROR) {
        nMBoxIcon = QMessageBox::Critical;
        nNotifyIcon = Notificator::Critical;
    } else if (style & CClientUIInterface::ICON_WARNING) {
        nMBoxIcon = QMessageBox::Warning;
        nNotifyIcon = Notificator::Warning;
    }

    if (style & CClientUIInterface::MODAL) {
        // Check for buttons, use OK as default, if none was supplied
        QMessageBox::StandardButton buttons;
        if (!(buttons = (QMessageBox::StandardButton)(style & CClientUIInterface::BTN_MASK)))
            buttons = QMessageBox::Ok;

        showNormalIfMinimized();
        QMessageBox mBox(static_cast<QMessageBox::Icon>(nMBoxIcon), strTitle, message, buttons, this);
        mBox.setTextFormat(Qt::PlainText);
        mBox.setDetailedText(detailed_message);
        int r = mBox.exec();
        if (ret != nullptr)
            *ret = r == QMessageBox::Ok;
    } else {
        notificator->notify(static_cast<Notificator::Class>(nNotifyIcon), strTitle, message);
    }
}

void BitcoinGUI::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::PaletteChange) {
        overviewAction->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/overview")));
        sendCoinsAction->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/send")));
        receiveCoinsAction->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/receiving_addresses")));
        historyAction->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/history")));
        mintingAction->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/staking")));
        imageLogo->setPixmap(createLogo());
    }

    QMainWindow::changeEvent(e);

#ifndef Q_OS_MAC // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel() && clientModel->getOptionsModel()->getMinimizeToTray())
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, &BitcoinGUI::hide);
                e->ignore();
            }
            else if((wsevt->oldState() & Qt::WindowMinimized) && !isMinimized())
            {
                QTimer::singleShot(0, this, &BitcoinGUI::show);
                e->ignore();
            }
        }
    }
#endif
}

void BitcoinGUI::closeEvent(QCloseEvent *event)
{
#ifndef Q_OS_MAC // Ignored on Mac
    if(clientModel && clientModel->getOptionsModel())
    {
        if(!clientModel->getOptionsModel()->getMinimizeOnClose())
        {
            // close rpcConsole in case it was open to make some space for the shutdown window
            rpcConsole->close();

            QApplication::quit();
        }
        else
        {
            QMainWindow::showMinimized();
            event->ignore();
        }
    }
#else
    QMainWindow::closeEvent(event);
#endif
}

void BitcoinGUI::showEvent(QShowEvent *event)
{
    // enable the debug window when the main window shows up
    openRPCConsoleAction->setEnabled(true);
    aboutAction->setEnabled(true);
    optionsAction->setEnabled(true);
}

#ifdef ENABLE_WALLET
void BitcoinGUI::incomingTransaction(const QString& date, int unit, const CAmount& amount, const QString& type, const QString& address, const QString& label, const QString& walletName)
{
    // On new transaction, make an info balloon
    QString msg = tr("Date: %1\n").arg(date) +
                  tr("Amount: %1\n").arg(BitcoinUnits::formatWithUnit(unit, amount, true));
    if (m_node.walletClient().getWallets().size() > 1 && !walletName.isEmpty()) {
        msg += tr("Wallet: %1\n").arg(walletName);
    }
    msg += tr("Type: %1\n").arg(type);
    if (!label.isEmpty())
        msg += tr("Label: %1\n").arg(label);
    else if (!address.isEmpty())
        msg += tr("Address: %1\n").arg(address);
    message((amount)<0 ? tr("Sent transaction") : tr("Incoming transaction"),
             msg, CClientUIInterface::MSG_INFORMATION);
}
#endif // ENABLE_WALLET

void BitcoinGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void BitcoinGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        for (const QUrl &uri : event->mimeData()->urls())
        {
            Q_EMIT receivedURI(uri.toString());
        }
    }
    event->acceptProposedAction();
}

bool BitcoinGUI::eventFilter(QObject *object, QEvent *event)
{
    // Catch status tip events
    if (event->type() == QEvent::StatusTip)
    {
        // Prevent adding text from setStatusTip(), if we currently use the status bar for displaying other stuff
        if (progressBarLabel->isVisible() || progressBar->isVisible())
            return true;
    }
    return QMainWindow::eventFilter(object, event);
}

#ifdef ENABLE_WALLET
bool BitcoinGUI::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    // URI has to be valid
    if (walletFrame && walletFrame->handlePaymentRequest(recipient))
    {
        showNormalIfMinimized();
        gotoSendCoinsPage();
        return true;
    }
    return false;
}

void BitcoinGUI::setHDStatus(bool privkeyDisabled, int hdEnabled)
{
    QString icon = "";
    if (hdEnabled == HD_DISABLED) {
        icon = ":/icons/hd_disabled";
    } else if (hdEnabled == HD_ENABLED_32) {
        icon = ":/icons/hd_enabled_32";
    } else if (hdEnabled == HD_ENABLED_39) {
        icon = ":/icons/hd_enabled_39";
    } else if (hdEnabled == HD_ENABLED_44) {
        icon = ":/icons/hd_enabled_44";
    }

    labelWalletHDStatusIcon->setThemedPixmap(privkeyDisabled ? QStringLiteral(":/icons/eye") : icon, STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE);
    labelWalletHDStatusIcon->setToolTip(privkeyDisabled ? tr("Private key <b>disabled</b>") : hdEnabled > 0 ? tr("HD key generation is <b>enabled</b>") : tr("HD key generation is <b>disabled</b>"));
    labelWalletHDStatusIcon->show();
    // eventually disable the QLabel to set its opacity to 50%
    labelWalletHDStatusIcon->setEnabled(hdEnabled > 0);
}

void BitcoinGUI::setWalletLocked(bool wallet_locked)
{
    m_lock_context_menu->clear();
    m_lock_context_menu->addAction(
        wallet_locked ?
            //: A context menu item.
            tr("Unlock Wallet") :
            //: A context menu item. The stake state activity was unlocked previously.
            tr("Lock Wallet"),
        [this, new_state = !wallet_locked] { walletFrame->lockWallet(new_state); });
}

void BitcoinGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::NoKeys:
        labelWalletEncryptionIcon->hide();
        encryptWalletAction->setChecked(false);
        changePassphraseAction->setEnabled(false);
        encryptWalletAction->setEnabled(false);
        break;
    case WalletModel::Unencrypted:
        labelWalletEncryptionIcon->hide();
        encryptWalletAction->setChecked(false);
        changePassphraseAction->setEnabled(false);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(true);
        break;
    case WalletModel::Unlocked:
        labelWalletEncryptionIcon->show();
        labelWalletEncryptionIcon->setThemedPixmap(QStringLiteral(":/icons/lock_open"), STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE);
        labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(true);
        encryptWalletAction->setEnabled(false);
        setWalletLocked(false);
        break;
    case WalletModel::Locked:
        labelWalletEncryptionIcon->show();
        labelWalletEncryptionIcon->setThemedPixmap(QStringLiteral(":/icons/lock_closed"), STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE);
        labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>locked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(true);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(false);
        setWalletLocked(true);
        break;
    }

    updateWalletStakingStatus();
}

void BitcoinGUI::setWalletStakingActive(bool staking_active)
{
    qDebug() << QString("BitcoinGUI::%1: Staking updated to %2").arg(__func__).arg(staking_active);
    updateWalletStakingStatus();

    enableStakingAction->setVisible(!staking_active);
    disableStakingAction->setVisible(staking_active);

    m_wallet_staking_context_menu->clear();
    m_wallet_staking_context_menu->addAction(
        //: A context menu item. The "Stake tab" is an element of the "Node window".
        tr("Show Staking tab"),
        [this] {
            rpcConsole->setTabFocus(RPCConsole::TabTypes::STAKE);
            showDebugWindow();
        });
    m_wallet_staking_context_menu->addAction(
        staking_active ?
            //: A context menu item.
            tr("Disable Staking") :
            //: A context menu item. The stake state activity was disabled previously.
            tr("Enable Staking"),
            [this, new_state = !staking_active] { walletFrame->enableStaking(new_state); });
}

void BitcoinGUI::updateWalletStakingStatus()
{
    if (!walletFrame) {
        return;
    }
    WalletView* const walletView = walletFrame->currentWalletView();
    if (!walletView) {
        return;
    }
    WalletModel* const walletModel = walletView->getWalletModel();
    if (!walletModel) {
        return;
    }
    uint64_t nAverageWeight = 0, nTotalWeight = 0;
    int64_t nLastCoinStakeSearchInterval;
    bool staking = false;

    QString msg;
    QString icon = "";
    if (walletModel) {
        qDebug() << QString("BitcoinGUI::%1: wallet %2 updated").arg(__func__).arg(walletModel->getDisplayName());
        walletModel->GetStakeWeight(nAverageWeight, nTotalWeight);
        nLastCoinStakeSearchInterval = walletModel->wallet().getLastCoinStakeSearchInterval();
        staking = nLastCoinStakeSearchInterval && nAverageWeight;
    }

    if (staking && m_node.getNodeStakingActive()) {
        msg = tr("Wallet is staking");
        icon = ":/icons/staking_on";
    } else {
        if (!walletModel->getWalletStaking()) {
            msg = tr("Wallet staking is disabled");
            icon = ":/icons/staking_off";
        } else if (!m_node.getNodeStakingActive()) {
            msg = tr("Node Staking is not enabled");
            icon = ":/icons/warning";
        } else if (walletModel->getEncryptionStatus() == WalletModel::Locked) {
            msg = tr("Not staking because wallet is locked");
            icon = ":/icons/warning";
        } else if (!m_node.getNetworkActive()) {
            msg = tr("Not staking because wallet is offline");
            icon = ":/icons/warning";
        } else if (m_node.isInitialBlockDownload()) {
            msg = tr("Not staking because wallet is syncing");
            icon = ":/icons/warning";
        } else if (!nAverageWeight) {
            msg = tr("Not staking because you don't have mature coins");
            icon = ":/icons/warning";
        } else if (!staking) {
            msg = tr("Waiting for staking to start");
            icon = ":/icons/warning";
        } else {
            icon = ":/icons/staking_off";
        }
    }

    walletstakingStatusControl->setToolTip(msg);
    walletstakingStatusControl->setThemedPixmap(icon, STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE);
}


void BitcoinGUI::setNodeStakingActive(bool staking_active)
{
    qDebug() << QString("BitcoinGUI::%1: Staking updated to %2").arg(__func__).arg(staking_active);
    updateNodeStakingStatus();

    m_node_staking_context_menu->clear();
    m_node_staking_context_menu->addAction(
        staking_active ?
            //: A context menu item.
            tr("Disable Node Staking") :
            //: A context menu item. The stake state activity was disabled previously.
            tr("Enable Node Staking"),
        [this, new_state = !staking_active] { m_node.setNodeStakingActive(new_state); });
}

void BitcoinGUI::updateNodeStakingStatus()
{
  qDebug() << QString("BitcoinGUI::%1: Staking updated to %2").arg(__func__).arg(m_node.getNodeStakingActive());
  QString msg;
  if (m_node.getNodeStakingActive()) {
      msg = tr("Staking is enabled");
      nodestakingStatusControl->setThemedPixmap(QStringLiteral(":/icons/global_staking_on"), STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE);
  } else {
      msg = tr("Staking is disabled");
      nodestakingStatusControl->setThemedPixmap(QStringLiteral(":/icons/global_staking_off"), STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE);
  }

  nodestakingStatusControl->setToolTip(msg);

  updateWalletStakingStatus();
}

void BitcoinGUI::updateWalletStatus()
{
    if (!walletFrame) {
        return;
    }
    WalletView * const walletView = walletFrame->currentWalletView();
    if (!walletView) {
        return;
    }
    WalletModel * const walletModel = walletView->getWalletModel();
    setEncryptionStatus(walletModel->getEncryptionStatus());
    int hdType = HD_DISABLED;
    if (walletModel->wallet().bip44Enabled()) {
        hdType = HD_ENABLED_44;
    } else if (walletModel->wallet().bip39Enabled()) {
        hdType = HD_ENABLED_39;
    } else if (walletModel->wallet().hdEnabled()) {
        hdType = HD_ENABLED_32;
    }
    setHDStatus(walletModel->wallet().privateKeysDisabled(), hdType);
}
#endif // ENABLE_WALLET

void BitcoinGUI::updateProxyIcon()
{
    std::string ip_port;
    bool proxy_enabled = clientModel->getProxyInfo(ip_port);

    if (proxy_enabled) {
        if (!GUIUtil::HasPixmap(labelProxyIcon)) {
            QString ip_port_q = QString::fromStdString(ip_port);
            labelProxyIcon->setThemedPixmap((":/icons/proxy"), STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE);
            labelProxyIcon->setToolTip(tr("Proxy is <b>enabled</b>: %1").arg(ip_port_q));
        } else {
            labelProxyIcon->show();
        }
    } else {
        labelProxyIcon->hide();
    }
}

void BitcoinGUI::updateWindowTitle()
{
    QString window_title = PACKAGE_NAME;
#ifdef ENABLE_WALLET
    if (walletFrame) {
        WalletModel* const wallet_model = walletFrame->currentWalletModel();
        if (wallet_model && !wallet_model->getWalletName().isEmpty()) {
            window_title += " - " + wallet_model->getDisplayName();
        }
    }
#endif
    if (!m_network_style->getTitleAddText().isEmpty()) {
        window_title += " - " + m_network_style->getTitleAddText();
    }
    setWindowTitle(window_title);
}

void BitcoinGUI::showNormalIfMinimized(bool fToggleHidden)
{
    if(!clientModel)
        return;

    if (!isHidden() && !isMinimized() && !GUIUtil::isObscured(this) && fToggleHidden) {
        hide();
    } else {
        GUIUtil::bringToFront(this);
    }
}

void BitcoinGUI::toggleHidden()
{
    showNormalIfMinimized(true);
}

void BitcoinGUI::detectShutdown()
{
    if (m_node.shutdownRequested())
    {
        if(rpcConsole)
            rpcConsole->hide();
        qApp->quit();
    }
}

void BitcoinGUI::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0) {
        progressDialog = new QProgressDialog(title, QString(), 0, 100);
        GUIUtil::PolishProgressDialog(progressDialog);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    } else if (nProgress == 100) {
        if (progressDialog) {
            progressDialog->close();
            progressDialog->deleteLater();
            progressDialog = nullptr;
        }
    } else if (progressDialog) {
        progressDialog->setValue(nProgress);
    }
}

void BitcoinGUI::showModalOverlay()
{
    if (modalOverlay && (progressBar->isVisible() || modalOverlay->isLayerVisible()))
        modalOverlay->toggleVisibility();
}

void BitcoinGUI::updateTheme(const QString& themeName)
{
    platformStyle->SetTheme(themeName);
}

void BitcoinGUI::updateStyle(const QString& styleName)
{
    QFile styleFile(":/themes/" + styleName);
    if (!styleFile.open(QFile::ReadOnly)) {
        return;
    }
    QString styleSheet = QLatin1String(styleFile.readAll());
    this->setStyleSheet(styleSheet);
}

static bool ThreadSafeMessageBox(BitcoinGUI* gui, const bilingual_str& message, const std::string& caption, unsigned int style)
{
    bool modal = (style & CClientUIInterface::MODAL);
    // The SECURE flag has no effect in the Qt GUI.
    // bool secure = (style & CClientUIInterface::SECURE);
    style &= ~CClientUIInterface::SECURE;
    bool ret = false;

    QString detailed_message; // This is original message, in English, for googling and referencing.
    if (message.original != message.translated) {
        detailed_message = BitcoinGUI::tr("Original message:") + "\n" + QString::fromStdString(message.original);
    }

    // In case of modal message, use blocking connection to wait for user to click a button
    bool invoked = QMetaObject::invokeMethod(gui, "message",
                               modal ? GUIUtil::blockingGUIThreadConnection() : Qt::QueuedConnection,
                               Q_ARG(QString, QString::fromStdString(caption)),
                               Q_ARG(QString, QString::fromStdString(message.translated)),
                               Q_ARG(unsigned int, style),
                               Q_ARG(bool*, &ret),
                               Q_ARG(QString, detailed_message));
    assert(invoked);
    return ret;
}

void BitcoinGUI::subscribeToCoreSignals()
{
    // Connect signals to client
    m_handler_message_box = m_node.handleMessageBox(std::bind(ThreadSafeMessageBox, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    m_handler_question = m_node.handleQuestion(std::bind(ThreadSafeMessageBox, this, std::placeholders::_1, std::placeholders::_3, std::placeholders::_4));
}

void BitcoinGUI::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    m_handler_message_box->disconnect();
    m_handler_question->disconnect();
}

bool BitcoinGUI::isPrivacyModeActivated() const
{
    assert(m_mask_values_action);
    return m_mask_values_action->isChecked();
}

UnitDisplayStatusBarControl::UnitDisplayStatusBarControl(const PlatformStyle *platformStyle)
    : optionsModel(nullptr),
      menu(nullptr),
      m_platform_style{platformStyle}
{
    createContextMenu();
    setToolTip(tr("Unit to show amounts in. Click to select another unit."));
    QList<BitcoinUnits::Unit> units = BitcoinUnits::availableUnits();
    int max_width = 0;
    const QFontMetrics fm(font());
    for (const BitcoinUnits::Unit unit : units)
    {
        max_width = qMax(max_width, GUIUtil::TextWidth(fm, BitcoinUnits::longName(unit)));
    }
    setMinimumSize(max_width, 0);
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QPalette palette;
    palette.setColor(QPalette::WindowText, m_platform_style->SingleColor());
    setPalette(palette);
}

/** So that it responds to button clicks */
void UnitDisplayStatusBarControl::mousePressEvent(QMouseEvent *event)
{
    onDisplayUnitsClicked(event->pos());
}

void UnitDisplayStatusBarControl::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange) {
        QPalette palette;
        palette.setColor(QPalette::WindowText, m_platform_style->SingleColor());
        setPalette(palette);
    }

    QLabel::changeEvent(e);
}

/** Creates context menu, its actions, and wires up all the relevant signals for mouse events. */
void UnitDisplayStatusBarControl::createContextMenu()
{
    menu = new QMenu(this);
    for (const BitcoinUnits::Unit u : BitcoinUnits::availableUnits()) {
        menu->addAction(BitcoinUnits::longName(u))->setData(QVariant(u));
    }
    connect(menu, &QMenu::triggered, this, &UnitDisplayStatusBarControl::onMenuSelection);
}

/** Lets the control know about the Options Model (and its signals) */
void UnitDisplayStatusBarControl::setOptionsModel(OptionsModel *_optionsModel)
{
    if (_optionsModel)
    {
        this->optionsModel = _optionsModel;

        // be aware of a display unit change reported by the OptionsModel object.
        connect(_optionsModel, &OptionsModel::displayUnitChanged, this, &UnitDisplayStatusBarControl::updateDisplayUnit);

        // initialize the display units label with the current value in the model.
        updateDisplayUnit(_optionsModel->getDisplayUnit());
    }
}

/** When Display Units are changed on OptionsModel it will refresh the display text of the control on the status bar */
void UnitDisplayStatusBarControl::updateDisplayUnit(int newUnits)
{
    setText(BitcoinUnits::longName(newUnits));
}

/** Shows context menu with Display Unit options by the mouse coordinates */
void UnitDisplayStatusBarControl::onDisplayUnitsClicked(const QPoint& point)
{
    QPoint globalPos = mapToGlobal(point);
    menu->exec(globalPos);
}

/** Tells underlying optionsModel to update its current display unit. */
void UnitDisplayStatusBarControl::onMenuSelection(QAction* action)
{
    if (action)
    {
        optionsModel->setDisplayUnit(action->data());
    }
}

