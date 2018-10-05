// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX developers 
// Copyright (c) 2018 The Wino developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "db.h"
#include "init.h"
#include "main.h"
#include "masternode-budget.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "rpcserver.h"
#include "utilmoneystr.h"

#include <fstream>
using namespace json_spirit;

void SendMoney(const CTxDestination& address, CAmount nValue, CWalletTx& wtxNew, AvailableCoinsType coin_type = ALL_COINS)
{
    // Check amount
    if (nValue <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    if (nValue > pwalletMain->GetBalance())
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

    string strError;
    if (pwalletMain->IsLocked()) {
        strError = "Error: Wallet locked, unable to create transaction!";
        LogPrintf("SendMoney() : %s", strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    // Parse WINO address
    CScript scriptPubKey = GetScriptForDestination(address);

    // Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    if (!pwalletMain->CreateTransaction(scriptPubKey, nValue, wtxNew, reservekey, nFeeRequired, strError, NULL, coin_type)) {
        if (nValue + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        LogPrintf("SendMoney() : %s\n", strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if (!pwalletMain->CommitTransaction(wtxNew, reservekey))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
}

Value Darksend(const Array& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error(
            "Darksend <winoaddress> <amount>\n"
            "winoaddress, reset, or auto (AutoDenominate)"
            "<amount> is a real and will be rounded to the next 0.1" +
            HelpRequiringPassphrase());

    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");

    if (params[0].get_str() == "auto") {
        if (fMasterNode)
            return "DarKsend is not supported from masternodes";

        return "DoAutomaticDenominating " + (DarKsendPool.DoAutomaticDenominating() ? "successful" : ("failed: " + DarKsendPool.GetStatus()));
    }

    if (params[0].get_str() == "reset") {
        DarKsendPool.Reset();
        return "successfully reset Darksend";
    }

    if (params.size() != 2)
        throw runtime_error(
            "Darksend <winoaddress> <amount>\n"
            "winoaddress, denominate, or auto (AutoDenominate)"
            "<amount> is a real and will be rounded to the next 0.1" +
            HelpRequiringPassphrase());

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Wino address");

    // Amount
    CAmount nAmount = AmountFromValue(params[1]);

    // Wallet comments
    CWalletTx wtx;
    //    string strError = pwalletMain->SendMoneyToDestination(address.Get(), nAmount, wtx, ONLY_DENOMINATED);
    SendMoney(address.Get(), nAmount, wtx, ONLY_DENOMINATED);
    //    if (strError != "")
    //        throw JSONRPCError(RPC_WALLET_ERROR, strError);

    return wtx.GetHash().GetHex();
}


Value getpoolinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getpoolinfo\n"
            "Returns an object containing anonymous pool-related information.");

    Object obj;
    obj.push_back(Pair("current_masternode", mnodeman.GetCurrentMasterNode()->addr.ToString()));
    obj.push_back(Pair("state", DarKsendPool.GetState()));
    obj.push_back(Pair("entries", DarKsendPool.GetEntriesCount()));
    obj.push_back(Pair("entries_accepted", DarKsendPool.GetCountEntriesAccepted()));
    return obj;
}


Value masternode(const Array& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp ||
        (strCommand != "start" && strCommand != "start-alias" && strCommand != "start-many" && strCommand != "start-all" && strCommand != "start-missing" &&
            strCommand != "start-disabled" && strCommand != "list" && strCommand != "list-conf" && strCommand != "count" && strCommand != "enforce" &&
            strCommand != "debug" && strCommand != "current" && strCommand != "winners" && strCommand != "genkey" && strCommand != "connect" &&
            strCommand != "outputs" && strCommand != "status" && strCommand != "calcscore"))
        throw runtime_error(
            "masternode \"command\"... ( \"passphrase\" )\n"
            "Set of commands to execute masternode related actions\n"
            "\nArguments:\n"
            "1. \"command\"        (string or set of strings, required) The command to execute\n"
            "2. \"passphrase\"     (string, optional) The wallet passphrase\n"
            "\nAvailable commands:\n"
            "  count        - Print number of all known masternodes (optional: 'obf', 'enabled', 'all', 'qualify')\n"
            "  current      - Print info on current masternode winner\n"
            "  debug        - Print masternode status\n"
            "  genkey       - Generate new masternodeprivkey\n"
            "  enforce      - Enforce masternode payments\n"
            "  outputs      - Print masternode compatible outputs\n"
            "  start        - Start masternode configured in wino.conf\n"
            "  start-alias  - Start single masternode by assigned alias configured in masternode.conf\n"
            "  start-<mode> - Start masternodes configured in masternode.conf (<mode>: 'all', 'missing', 'disabled')\n"
            "  status       - Print masternode status information\n"
            "  list         - Print list of all known masternodes (see masternodelist for more info)\n"
            "  list-conf    - Print masternode.conf in JSON format\n"
            "  winners      - Print list of masternode winners\n");

    if (strCommand == "list") {
        Array newParams(params.size() - 1);
        std::copy(params.begin() + 1, params.end(), newParams.begin());
        return masternodelist(newParams, fHelp);
    }

    if (strCommand == "budget") {
        return "Show budgets";
    }

    if (strCommand == "connect") {
        std::string strAddress = "";
        if (params.size() == 2) {
            strAddress = params[1].get_str();
        } else {
            throw runtime_error("Masternode address required\n");
        }

        CService addr = CService(strAddress);

        CNode* pnode = ConnectNode((CAddress)addr, NULL, false);
        if (pnode) {
            pnode->Release();
            return "successfully connected";
        } else {
            throw runtime_error("error connecting\n");
        }
    }

    if (strCommand == "count") {
        if (params.size() > 2) {
            throw runtime_error("too many parameters\n");
        }
        if (params.size() == 2) {
            int nCount = 0;

            if (chainActive.Tip())
                mnodeman.GetNextMasternodeInQueueForPayment(chainActive.Tip()->nHeight, true, nCount);

            if (params[1] == "obf") return mnodeman.CountEnabled(ActiveProtocol());
            if (params[1] == "enabled") return mnodeman.CountEnabled();
            if (params[1] == "qualify") return nCount;
            if (params[1] == "all") return strprintf("Total: %d (OBF Compatible: %d / Enabled: %d / Qualify: %d)",
                mnodeman.size(),
                mnodeman.CountEnabled(ActiveProtocol()),
                mnodeman.CountEnabled(),
                nCount);
        }
        return mnodeman.size();
    }

    if (strCommand == "current") {
        CMasternode* winner = mnodeman.GetCurrentMasterNode(1);
        if (winner) {
            Object obj;

            obj.push_back(Pair("IP:port", winner->addr.ToString()));
            obj.push_back(Pair("protocol", (int64_t)winner->protocolVersion));
            obj.push_back(Pair("vin", winner->vin.prevout.hash.ToString()));
            obj.push_back(Pair("pubkey", CBitcoinAddress(winner->pubKeyCollateralAddress.GetID()).ToString()));
            obj.push_back(Pair("lastseen", (winner->lastPing == CMasternodePing()) ? winner->sigTime : (int64_t)winner->lastPing.sigTime));
            obj.push_back(Pair("activeseconds", (winner->lastPing == CMasternodePing()) ? 0 : (int64_t)(winner->lastPing.sigTime - winner->sigTime)));
            return obj;
        }

        return "unknown";
    }

    if (strCommand == "debug") {
        if (activeMasternode.status != ACTIVE_MASTERNODE_INITIAL || !masternodeSync.IsSynced())
            return activeMasternode.GetStatus();

        CTxIn vin = CTxIn();
        CPubKey pubkey = CScript();
        CKey key;
        bool found = activeMasternode.GetMasterNodeVin(vin, pubkey, key);
        if (!found) {
            throw runtime_error("Missing masternode input, please look at the documentation for instructions on masternode creation\n");
        } else {
            return activeMasternode.GetStatus();
        }
    }

    if (strCommand == "enforce") {
        return (uint64_t)enforceMasternodePaymentsTime;
    }

    if (strCommand == "start") {
        if (!fMasterNode) throw runtime_error("you must set masternode=1 in the configuration\n");

        if (pwalletMain->IsLocked()) {
            SecureString strWalletPass;
            strWalletPass.reserve(100);

            if (params.size() == 2) {
                strWalletPass = params[1].get_str().c_str();
            } else {
                throw runtime_error("Your wallet is locked, passphrase is required\n");
            }

            if (!pwalletMain->Unlock(strWalletPass)) {
                throw runtime_error("incorrect passphrase\n");
            }
        }

        if (activeMasternode.status != ACTIVE_MASTERNODE_STARTED) {
            activeMasternode.status = ACTIVE_MASTERNODE_INITIAL; // TODO: consider better way
            activeMasternode.ManageStatus();
            pwalletMain->Lock();
        }

        return activeMasternode.GetStatus();
    }

    if (strCommand == "start-alias") {
        if (params.size() < 2) {
            throw runtime_error("command needs at least 2 parameters\n");
        }

        std::string alias = params[1].get_str();

        if (pwalletMain->IsLocked()) {
            SecureString strWalletPass;
            strWalletPass.reserve(100);

            if (params.size() == 3) {
                strWalletPass = params[2].get_str().c_str();
            } else {
                throw runtime_error("Your wallet is locked, passphrase is required\n");
            }

            if (!pwalletMain->Unlock(strWalletPass)) {
                throw runtime_error("incorrect passphrase\n");
            }
        }

        bool found = false;

        Object statusObj;
        statusObj.push_back(Pair("alias", alias));

        BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
            if (mne.getAlias() == alias) {
                found = true;
                std::string errorMessage;

                bool result = activeMasternode.Register(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage);

                statusObj.push_back(Pair("result", result ? "successful" : "failed"));
                if (!result) {
                    statusObj.push_back(Pair("errorMessage", errorMessage));
                }
                break;
            }
        }

        if (!found) {
            statusObj.push_back(Pair("result", "failed"));
            statusObj.push_back(Pair("errorMessage", "could not find alias in config. Verify with list-conf."));
        }

        pwalletMain->Lock();
        return statusObj;
    }

    if (strCommand == "start-many" || strCommand == "start-all" || strCommand == "start-missing" || strCommand == "start-disabled") {
        if (pwalletMain->IsLocked()) {
            SecureString strWalletPass;
            strWalletPass.reserve(100);

            if (params.size() == 2) {
                strWalletPass = params[1].get_str().c_str();
            } else {
                throw runtime_error("Your wallet is locked, passphrase is required\n");
            }

            if (!pwalletMain->Unlock(strWalletPass)) {
                throw runtime_error("incorrect passphrase\n");
            }
        }

        if ((strCommand == "start-missing" || strCommand == "start-disabled") &&
            (masternodeSync.RequestedMasternodeAssets <= MASTERNODE_SYNC_LIST ||
                masternodeSync.RequestedMasternodeAssets == MASTERNODE_SYNC_FAILED)) {
            throw runtime_error("You can't use this command until masternode list is synced\n");
        }

        std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
        mnEntries = masternodeConfig.getEntries();

        int successful = 0;
        int failed = 0;

        Object resultsObj;

        BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
            std::string errorMessage;
            int nIndex;
            if(!mne.castOutputIndex(nIndex))
                continue;
            CTxIn vin = CTxIn(uint256(mne.getTxHash()), uint32_t(nIndex));
            CMasternode* pmn = mnodeman.Find(vin);

            if (strCommand == "start-missing" && pmn) continue;
            if (strCommand == "start-disabled" && pmn && pmn->IsEnabled()) continue;

            bool result = activeMasternode.Register(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage);

            Object statusObj;
            statusObj.push_back(Pair("alias", mne.getAlias()));
            statusObj.push_back(Pair("result", result ? "successful" : "failed"));

            if (result) {
                successful++;
            } else {
                failed++;
                statusObj.push_back(Pair("errorMessage", errorMessage));
            }

            resultsObj.push_back(Pair("status", statusObj));
        }
        pwalletMain->Lock();

        Object returnObj;
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d masternodes, failed to start %d, total %d", successful, failed, successful + failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    if (strCommand == "create") {
        throw runtime_error("Not implemented yet, please look at the documentation for instructions on masternode creation\n");
    }

    if (strCommand == "genkey") {
        CKey secret;
        secret.MakeNewKey(false);

        return CBitcoinSecret(secret).ToString();
    }

    if (strCommand == "list-conf") {
        std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
        mnEntries = masternodeConfig.getEntries();

        Object resultObj;

        BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
            int nIndex;
            if(!mne.castOutputIndex(nIndex))
                continue;
            CTxIn vin = CTxIn(uint256(mne.getTxHash()), uint32_t(nIndex));
            CMasternode* pmn = mnodeman.Find(vin);

            std::string strStatus = pmn ? pmn->Status() : "MISSING";

            Object mnObj;
            mnObj.push_back(Pair("alias", mne.getAlias()));
            mnObj.push_back(Pair("address", mne.getIp()));
            mnObj.push_back(Pair("privateKey", mne.getPrivKey()));
            mnObj.push_back(Pair("txHash", mne.getTxHash()));
            mnObj.push_back(Pair("outputIndex", mne.getOutputIndex()));
            mnObj.push_back(Pair("status", strStatus));
            resultObj.push_back(Pair("masternode", mnObj));
        }

        return resultObj;
    }

    if (strCommand == "outputs") {
        // Find possible candidates
        vector<COutput> possibleCoins = activeMasternode.SelectCoinsMasternode();

        Object obj;
        BOOST_FOREACH (COutput& out, possibleCoins) {
            obj.push_back(Pair(out.tx->GetHash().ToString(), strprintf("%d", out.i)));
        }

        return obj;
    }

    if (strCommand == "status") {
        if (!fMasterNode) throw runtime_error("This is not a masternode\n");

        Object mnObj;
        CMasternode* pmn = mnodeman.Find(activeMasternode.vin);

        mnObj.push_back(Pair("vin", activeMasternode.vin.ToString()));
        mnObj.push_back(Pair("service", activeMasternode.service.ToString()));
        if (pmn) mnObj.push_back(Pair("pubkey", CBitcoinAddress(pmn->pubKeyCollateralAddress.GetID()).ToString()));
        mnObj.push_back(Pair("status", activeMasternode.GetStatus()));
        return mnObj;
    }

    if (strCommand == "winners") {
        int nLast = 10;

        if (params.size() >= 2) {
            try {
                nLast = std::stoi(params[1].get_str());
            } catch (const std::exception& e) {
                throw runtime_error("Exception on param 2");
            }
        }

        Object obj;

        for (int nHeight = chainActive.Tip()->nHeight - nLast; nHeight < chainActive.Tip()->nHeight + 20; nHeight++) {
            obj.push_back(Pair(strprintf("%d", nHeight), GetRequiredPaymentsString(nHeight)));
        }

        return obj;
    }

    /*
        Shows which masternode wins by score each block
    */
    if (strCommand == "calcscore") {
        int nLast = 10;

        if (params.size() >= 2) {
            try {
                nLast = std::stoi(params[1].get_str());
            } catch (const boost::bad_lexical_cast &) {
                throw runtime_error("Exception on param 2");
            }
        }
        Object obj;

        std::vector<CMasternode> vMasternodes = mnodeman.GetFullMasternodeVector();
        for (int nHeight = chainActive.Tip()->nHeight - nLast; nHeight < chainActive.Tip()->nHeight + 20; nHeight++) {
            uint256 nHigh = 0;
            CMasternode* pBestMasternode = NULL;
            BOOST_FOREACH (CMasternode& mn, vMasternodes) {
                uint256 n = mn.CalculateScore(1, nHeight - 100);
                if (n > nHigh) {
                    nHigh = n;
                    pBestMasternode = &mn;
                }
            }
            if (pBestMasternode)
                obj.push_back(Pair(strprintf("%d", nHeight), pBestMasternode->vin.prevout.ToStringShort().c_str()));
        }

        return obj;
    }

    return Value::null;
}

Value masternodelist(const Array& params, bool fHelp)
{
   std::string strFilter = "";

    if (params.size() == 1) strFilter = params[0].get_str();

    if (fHelp || (params.size() > 1))
        throw runtime_error(
            "listmasternodes ( \"filter\" )\n"
            "\nGet a ranked list of masternodes\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by txhash, status, or addr.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"rank\": n,           (numeric) Masternode Rank (or 0 if not enabled)\n"
            "    \"txhash\": \"hash\",    (string) Collateral transaction hash\n"
            "    \"outidx\": n,         (numeric) Collateral transaction output index\n"
            "    \"status\": s,         (string) Status (ENABLED/EXPIRED/REMOVE/etc)\n"
            "    \"addr\": \"addr\",      (string) Masternode Civitas address\n"
            "    \"version\": v,        (numeric) Masternode protocol version\n"
            "    \"lastseen\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last seen\n"
            "    \"activetime\": ttt,   (numeric) The time in seconds since epoch (Jan 1 1970 GMT) masternode has been active\n"
            "    \"lastpaid\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) masternode was last paid\n"
            "  }\n"
            "  ,...\n"
            "]\n");

    Array ret;
    int nHeight;
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if(!pindex) return 0;
        nHeight = pindex->nHeight;
    }
    std::vector<pair<int, CMasternode> > vMasternodeRanks = mnodeman.GetMasternodeRanks(nHeight);
    BOOST_FOREACH (PAIRTYPE(int, CMasternode) & s, vMasternodeRanks) {
        Object obj;
        std::string strVin = s.second.vin.prevout.ToStringShort();
        std::string strTxHash = s.second.vin.prevout.hash.ToString();
        uint32_t oIdx = s.second.vin.prevout.n;

        CMasternode* mn = mnodeman.Find(s.second.vin);

        if (mn != NULL) {
            if (strFilter != "" && strTxHash.find(strFilter) == string::npos &&
                mn->Status().find(strFilter) == string::npos &&
                CBitcoinAddress(mn->pubKeyCollateralAddress.GetID()).ToString().find(strFilter) == string::npos) continue;

            std::string strStatus = mn->Status();
            std::string strHost;
            int port;
            SplitHostPort(mn->addr.ToString(), port, strHost);
            CNetAddr node = CNetAddr(strHost, false);
            std::string strNetwork = GetNetworkName(node.GetNetwork());

            obj.push_back(Pair("rank", (strStatus == "ENABLED" ? s.first : 0)));
            obj.push_back(Pair("network", strNetwork));
            obj.push_back(Pair("txhash", strTxHash));
            obj.push_back(Pair("outidx", (uint64_t)oIdx));
            obj.push_back(Pair("status", strStatus));
            obj.push_back(Pair("addr", CBitcoinAddress(mn->pubKeyCollateralAddress.GetID()).ToString()));
            obj.push_back(Pair("version", mn->protocolVersion));
            obj.push_back(Pair("lastseen", (int64_t)mn->lastPing.sigTime));
            obj.push_back(Pair("activetime", (int64_t)(mn->lastPing.sigTime - mn->sigTime)));
            obj.push_back(Pair("lastpaid", (int64_t)mn->GetLastPaid()));

            ret.push_back(obj);
        }
    }

return ret;
}
