// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2019 The Privix Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternodelist.h"
#include "ui_masternodelist.h"

#include "configuremasternodepage.h"

#include "activemasternode.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "init.h"
#include "masternode-sync.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "sync.h"
#include "wallet.h"
#include "walletmodel.h"
#include "util.h"
#include "base58.h"

#include <QMessageBox>
#include <QTimer>
#include <fstream>
#include <iostream>
#include <string>

CCriticalSection cs_masternodes;

MasternodeList::MasternodeList(QWidget* parent) : QWidget(parent),
                                                  ui(new Ui::MasternodeList),
                                                  clientModel(0),
                                                  walletModel(0)
{
    ui->setupUi(this);

    ui->startButton->setEnabled(false);

    int columnAliasWidth = 100;
    int columnAddressWidth = 200;
    int columnProtocolWidth = 60;
    int columnStatusWidth = 80;
    int columnActiveWidth = 130;
    int columnLastSeenWidth = 130;

    ui->tableWidgetMyMasternodes->setAlternatingRowColors(true);
    ui->tableWidgetMyMasternodes->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(5, columnLastSeenWidth);

	ui->tableWidgetAllMasternodes->setAlternatingRowColors(true);
    ui->tableWidgetMyMasternodes->setColumnWidth(0, columnAddressWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(1, columnProtocolWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(2, columnStatusWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(3, columnActiveWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(4, columnLastSeenWidth);
	
    ui->tableWidgetMyMasternodes->setContextMenuPolicy(Qt::CustomContextMenu);

    ui->tableWidgetMyMasternodes->horizontalHeader()->setDefaultAlignment(Qt::AlignVCenter);

    QAction* startAliasAction = new QAction(tr("Start alias"), this);
    QAction* copyAliasAction = new QAction(tr("Copy alias"), this);
    QAction* editAliasAction = new QAction(tr("Edit alias"), this);
    QAction* deleteAliasAction = new QAction(tr("Delete"), this);	

    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    contextMenu->addAction(copyAliasAction);
    contextMenu->addAction(editAliasAction);
    contextMenu->addAction(deleteAliasAction);	
	
    connect(ui->tableWidgetMyMasternodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));	
    connect(copyAliasAction, SIGNAL(triggered()), this, SLOT(copyAlias()));	
    connect(editAliasAction, SIGNAL(triggered()), this, SLOT(on_editConfigureMasternode_clicked()));	
    connect(deleteAliasAction, SIGNAL(triggered()), this, SLOT(deleteAlias()));	
	
	
    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateNodeList()));
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    // Fill MN list
    fFilterUpdated = true;
    nTimeFilterUpdated = GetTime();

	updateNodeList();
}

MasternodeList::~MasternodeList()
{
    delete ui;
}

void MasternodeList::setClientModel(ClientModel* model)
{
    this->clientModel = model;
    if (model) {
        // try to update list when masternode count changes
        connect(clientModel, SIGNAL(strMasternodesChanged(QString)), this, SLOT(updateNodeList()));
    }
}

void MasternodeList::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
}

void MasternodeList::showContextMenu(const QPoint& point)
{
    QTableWidgetItem* item = ui->tableWidgetMyMasternodes->itemAt(point);
    if (item) contextMenu->exec(QCursor::pos());
}

void MasternodeList::updateNodeList()
{
    TRY_LOCK(cs_mnlist, fLockAcquired);
    if (!fLockAcquired) {
        return;
    }

    static int64_t nTimeListUpdated = GetTime();

    // to prevent high cpu usage update only once in MASTERNODELIST_UPDATE_SECONDS seconds
    // or MASTERNODELIST_FILTER_COOLDOWN_SECONDS seconds after filter was last changed
    int64_t nSecondsToWait = fFilterUpdated ? nTimeFilterUpdated - GetTime() + MASTERNODELIST_FILTER_COOLDOWN_SECONDS : nTimeListUpdated - GetTime() + MASTERNODELIST_UPDATE_SECONDS;

    if (fFilterUpdated) ui->nodeCount->setText(QString::fromStdString(strprintf("Please wait... %d", nSecondsToWait)));
    if (nSecondsToWait > 0) return;

    nTimeListUpdated = GetTime();
    fFilterUpdated = false;

    QString strToFilter;
    ui->nodeCount->setText("Updating...");
    ui->tableWidgetAllMasternodes->setSortingEnabled(false);
    ui->tableWidgetAllMasternodes->clearContents();
    ui->tableWidgetAllMasternodes->setRowCount(0);
    std::vector<CMasternode> vMasternodes = mnodeman.GetFullMasternodeVector();

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        // populate list
        // Address, Protocol, Status, Active Seconds, Last Seen, Pub Key
        QTableWidgetItem* addressItem = new QTableWidgetItem(QString::fromStdString(mn.addr.ToString()));
        QTableWidgetItem* protocolItem = new QTableWidgetItem(QString::number(mn.protocolVersion));
        QTableWidgetItem* statusItem = new QTableWidgetItem(QString::fromStdString(mn.GetStatus()));
        QTableWidgetItem* activeSecondsItem = new QTableWidgetItem(QString::fromStdString(DurationToDHMS(mn.lastPing.sigTime - mn.sigTime)));
        QTableWidgetItem* lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", mn.lastPing.sigTime)));
        QTableWidgetItem* pubkeyItem = new QTableWidgetItem(QString::fromStdString(EncodeDestination(CTxDestination(mn.pubKeyCollateralAddress.GetID()))));		

        if (strCurrentFilter != "") {
            strToFilter = addressItem->text() + " " +
                          protocolItem->text() + " " +
                          statusItem->text() + " " +
                          activeSecondsItem->text() + " " +
                          lastSeenItem->text() + " " +
                          pubkeyItem->text();
            if (!strToFilter.contains(strCurrentFilter)) continue;
        }

        ui->tableWidgetAllMasternodes->insertRow(0);
        ui->tableWidgetAllMasternodes->setItem(0, 0, addressItem);
        ui->tableWidgetAllMasternodes->setItem(0, 1, protocolItem);
        ui->tableWidgetAllMasternodes->setItem(0, 2, statusItem);
        ui->tableWidgetAllMasternodes->setItem(0, 3, activeSecondsItem);
        ui->tableWidgetAllMasternodes->setItem(0, 4, lastSeenItem);
        ui->tableWidgetAllMasternodes->setItem(0, 5, pubkeyItem);
    }

    ui->nodeCount->setText(QString::number(ui->tableWidgetAllMasternodes->rowCount()));
    ui->tableWidgetAllMasternodes->setSortingEnabled(true);
}


void MasternodeList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
        if (mne.getAlias() == strAlias) {
            std::string strError;
            CMasternodeBroadcast mnb;

            bool fSuccess = CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

            if (fSuccess) {
                strStatusHtml += "<br>Successfully started masternode.";
                mnodeman.UpdateMasternodeList(mnb);
                mnb.Relay();
            } else {
                strStatusHtml += "<br>Failed to start masternode.<br>Error: " + strError;
            }
            break;
        }
    }
    strStatusHtml += "</center>";

    QMessageBox msg;
    msg.setText(QString::fromStdString(strStatusHtml));
    msg.exec();

    updateMyNodeList(true);
}

void MasternodeList::on_filterLineEdit_textChanged(const QString& strFilterIn)
{
    strCurrentFilter = strFilterIn;
    nTimeFilterUpdated = GetTime();
    fFilterUpdated = true;
    ui->nodeCount->setText(QString::fromStdString(strprintf("Please wait... %d", MASTERNODELIST_FILTER_COOLDOWN_SECONDS)));
}

void MasternodeList::StartAll(std::string strCommand)
{
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
        std::string strError;
        CMasternodeBroadcast mnb;

        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CMasternode* pmn = mnodeman.Find(txin);

        if (strCommand == "start-missing" && pmn) continue;

        bool fSuccess = CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

        if (fSuccess) {
            nCountSuccessful++;
            mnodeman.UpdateMasternodeList(mnb);
            mnb.Relay();
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + mne.getAlias() + ". Error: " + strError;
        }
    }
    pwalletMain->Lock();

    std::string returnObj;
    returnObj = strprintf("Successfully started %d masternodes, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void MasternodeList::updateMyMasternodeInfo(QString strAlias, QString strAddr, CMasternode* pmn)
{
    LOCK(cs_mnlistupdate);

    bool fOldRowFound = false;
    int nNewRow = 0;
	
    for (int i = 0; i < ui->tableWidgetMyMasternodes->rowCount(); i++) {
        if (ui->tableWidgetMyMasternodes->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if (nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMyMasternodes->rowCount();
        ui->tableWidgetMyMasternodes->insertRow(nNewRow);
    }

    QTableWidgetItem* aliasItem = new QTableWidgetItem(strAlias);
    QTableWidgetItem* addrItem = new QTableWidgetItem(pmn ? QString::fromStdString(pmn->addr.ToString()) : strAddr);
    QTableWidgetItem* protocolItem = new QTableWidgetItem(QString::number(pmn ? pmn->protocolVersion : -1));
    QTableWidgetItem* statusItem = new QTableWidgetItem(QString::fromStdString(pmn ? pmn->GetStatus() : "MISSING"));
    GUIUtil::DHMSTableWidgetItem* activeSecondsItem = new GUIUtil::DHMSTableWidgetItem(pmn ? (pmn->lastPing.sigTime - pmn->sigTime) : 0);
    QTableWidgetItem* lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", pmn ? pmn->lastPing.sigTime : 0)));
    QTableWidgetItem* pubkeyItem = new QTableWidgetItem(QString::fromStdString(pmn ? EncodeDestination(CTxDestination(pmn->pubKeyCollateralAddress.GetID())) : ""));

    ui->tableWidgetMyMasternodes->setItem(nNewRow, 0, aliasItem);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 1, addrItem);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 2, protocolItem);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 3, statusItem);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 4, activeSecondsItem);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 5, lastSeenItem);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 6, pubkeyItem);
}

void MasternodeList::updateMyNodeList(bool fForce)
{
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my masternode list only once in MY_MASTERNODELIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_MASTERNODELIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel->setText(QString::number(nSecondsTillUpdate));

    if (nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    ui->tableWidgetMyMasternodes->setSortingEnabled(false);
    BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CMasternode* pmn = mnodeman.Find(txin);
        updateMyMasternodeInfo(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), pmn);
    }
    ui->tableWidgetMyMasternodes->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void MasternodeList::on_startButton_clicked()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMyMasternodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMyMasternodes->item(nSelectedRow, 0)->text().toStdString();

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm masternode start"),
        tr("Are you sure you want to start masternode %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
}

void MasternodeList::on_editConfigureMasternode_clicked()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMyMasternodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMyMasternodes->item(nSelectedRow, 0)->text().toStdString();

	int count = 0;
    BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
		count = count + 1;
		if(strAlias == mne.getAlias()) {
			MasternodeList::openEditConfigureMasternodePage(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), QString::fromStdString(mne.getPrivKey()), QString::fromStdString(mne.getTxHash()), QString::fromStdString(mne.getOutputIndex()), count);
			break;
			
		}
    }
}

void MasternodeList::on_configureMasternodeButton_clicked()
{
	
    ConfigureMasternodePage dlg(ConfigureMasternodePage::NewConfigureMasternode, this);
    if ( QDialog::Accepted == dlg.exec() )
    {
while (ui->tableWidgetMyMasternodes->rowCount() > 0)
	{
		ui->tableWidgetMyMasternodes->removeRow(0);
	}		
	
	// clear cache
	masternodeConfig.clear();
    // parse masternode.conf
    std::string strErr;
    if (!masternodeConfig.read(strErr)) {
        LogPrintf("Error reading masternode configuration file: \n");
    }	
      updateMyNodeList(true);
    }
}

void MasternodeList::openEditConfigureMasternodePage(QString strAlias, QString strIP, QString strPrivKey, QString strTxHash, QString strOutputIndex, int count)
{
    ConfigureMasternodePage dlg(ConfigureMasternodePage::EditConfigureMasternode, this);
    dlg.loadAlias(strAlias);
	dlg.loadIP(strIP);
	dlg.loadPrivKey(strPrivKey);
	dlg.loadTxHash(strTxHash);
	dlg.loadOutputIndex(strOutputIndex);
	dlg.counter(count);
	dlg.MNAliasCache(strAlias);
    if ( QDialog::Accepted == dlg.exec() )
    {
while (ui->tableWidgetMyMasternodes->rowCount() > 0)
	{
		ui->tableWidgetMyMasternodes->removeRow(0);
	}		
	
	// clear cache
	masternodeConfig.clear();
    // parse masternode.conf
    std::string strErr;
    if (!masternodeConfig.read(strErr)) {
        LogPrintf("Error reading masternode configuration file: \n");
    }	
      updateMyNodeList(true);
    }
}

void MasternodeList::deleteAlias()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMyMasternodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMyMasternodes->item(nSelectedRow, 0)->text().toStdString();
	int count = 0;
    BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
		count = count + 1;
		if(strAlias == mne.getAlias()) {
			vector<COutPoint> confLockedCoins;
			uint256 mnTxHash;
			mnTxHash.SetHex(mne.getTxHash());
            int nIndex;
            if(!mne.castOutputIndex(nIndex))
                continue;
			COutPoint outpoint = COutPoint(mnTxHash, nIndex);
			confLockedCoins.push_back(outpoint);
			pwalletMain->UnlockCoin(outpoint);
			masternodeConfig.deleteAlias(count);
			// write to masternode.conf
			masternodeConfig.writeToMasternodeConf();
while (ui->tableWidgetMyMasternodes->rowCount() > 0)
	{
		ui->tableWidgetMyMasternodes->removeRow(0);
	}		
	
	// clear cache
	masternodeConfig.clear();
    // parse masternode.conf
    std::string strErr;
    if (!masternodeConfig.read(strErr)) {
        LogPrintf("Error reading masternode configuration file: \n");
    }			
			updateMyNodeList(true);
			break;
			
		}
    }
}

void MasternodeList::copyAlias()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMyMasternodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMyMasternodes->item(nSelectedRow, 0)->text().toStdString();

    BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
		
		if(strAlias == mne.getAlias()) {
			
			std::string fullAliasCopy = mne.getAlias() + " " + mne.getIp() + " " + mne.getPrivKey() + " " + mne.getTxHash() + " " + mne.getOutputIndex();
			GUIUtil::setClipboard(QString::fromStdString(fullAliasCopy));
			break;
			
		}
    }
}

void MasternodeList::on_startAllButton_clicked()
{
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all masternodes start"),
        tr("Are you sure you want to start ALL masternodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll();
        return;
    }

    StartAll();
}

void MasternodeList::on_startMissingButton_clicked()
{
    if (!masternodeSync.IsMasternodeListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until masternode list is synced"));
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing masternodes start"),
        tr("Are you sure you want to start MISSING masternodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll("start-missing");
        return;
    }

    StartAll("start-missing");
}

void MasternodeList::on_tableWidgetMyMasternodes_itemSelectionChanged()
{
    if (ui->tableWidgetMyMasternodes->selectedItems().count() > 0) {
        ui->startButton->setEnabled(true);
    }
}

void MasternodeList::on_UpdateButton_clicked()
{
    updateMyNodeList(true);
}

