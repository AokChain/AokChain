// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionrecord.h"

#include "tokens/tokens.h"
#include "base58.h"
#include "consensus/consensus.h"
#include "validation.h"
#include "timedata.h"
#include "wallet/wallet.h"

#include <stdint.h>


/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction(const CWalletTx &wtx)
{
    // There are currently no cases where we hide transactions, but
    // we may want to use this in the future for things like RBF.
    return true;
}


/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const CWallet *wallet, const CWalletTx &wtx)
{
    QList<TransactionRecord> parts;
    int64_t nTime = wtx.GetTxTime();
    CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;
    uint256 hash = wtx.GetHash(), hashPrev;
    std::map<std::string, std::string> mapValue = wtx.mapValue;

    if (nNet > 0 || wtx.IsCoinBase() || wtx.IsCoinStake())
    {
        //
        // Credit
        //
        for(unsigned int i = 0; i < wtx.tx->vout.size(); i++)
        {
            const CTxOut& txout = wtx.tx->vout[i];
            isminetype mine = wallet->IsMine(txout);

            /** TOKENS START */
            if (txout.scriptPubKey.IsTokenScript())
                continue;
            /** TOKENS START */

            if(mine)
            {
                TransactionRecord sub(hash, nTime);
                CTxDestination address;
                sub.idx = i; // vout index
                sub.credit = txout.nValue;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (ExtractDestination(txout.scriptPubKey, address) && wallet->IsMineDest(address))
                {
                    // Received by AokChain Address
                    sub.type = TransactionRecord::RecvWithAddress;
                    sub.address = EncodeDestination(address);
                }
                else
                {
                    // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                    sub.type = TransactionRecord::RecvFromOther;
                    sub.address = mapValue["from"];
                }
                if (wtx.IsCoinBase() || wtx.IsCoinStake())
                {
                    // Generated
                    sub.type = TransactionRecord::Generated;
                }

                if (wtx.IsCoinStake())
                {
                    if (hashPrev == hash)
                        continue; // last coinstake output
                    sub.credit = nNet > 0 ? nNet : wtx.tx->GetValueOut() - nDebit;
                    hashPrev = hash;
                }

                parts.append(sub);
            }
        }
    }
    else
    {
        bool involvesWatchAddress = false;
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        for (const CTxIn& txin : wtx.tx->vin)
        {
            isminetype mine = wallet->IsMine(txin);
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllFromMe > mine) fAllFromMe = mine;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        for (const CTxOut& txout : wtx.tx->vout)
        {
            /** TOKENS START */
            if (txout.scriptPubKey.IsTokenScript())
                continue;
            /** TOKENS START */

            isminetype mine = wallet->IsMine(txout);
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllToMe > mine) fAllToMe = mine;
        }

        if (fAllFromMe && fAllToMe)
        {
            // Payment to self
            CAmount nChange = wtx.GetChange();

            parts.append(TransactionRecord(hash, nTime, TransactionRecord::SendToSelf, "",
                            -(nDebit - nChange), nCredit - nChange));
            parts.last().involvesWatchAddress = involvesWatchAddress;   // maybe pass to TransactionRecord as constructor argument
        }
        else if (fAllFromMe)
        {
            //
            // Debit
            //
            CAmount nTxFee = nDebit - wtx.tx->GetValueOut();

            for (unsigned int nOut = 0; nOut < wtx.tx->vout.size(); nOut++)
            {
                const CTxOut& txout = wtx.tx->vout[nOut];

                /** TOKENS START */
                if (txout.scriptPubKey.IsTokenScript())
                    continue;
                /** TOKENS START */

                TransactionRecord sub(hash, nTime);
                sub.idx = nOut;
                sub.involvesWatchAddress = involvesWatchAddress;

                if(wallet->IsMine(txout))
                {
                    // Ignore parts sent to self, as this is usually the change
                    // from a transaction sent back to our own address.
                    continue;
                }

                CTxDestination address;
                if (ExtractDestination(txout.scriptPubKey, address))
                {
                    // Sent to AokChain Address
                    sub.type = TransactionRecord::SendToAddress;
                    sub.address = EncodeDestination(address);
                }
                else
                {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.type = TransactionRecord::SendToOther;
                    sub.address = mapValue["to"];
                }

                CAmount nValue = txout.nValue;
                /* Add fee to first output */
                if (nTxFee > 0)
                {
                    nValue += nTxFee;
                    nTxFee = 0;
                }
                sub.debit = -nValue;

                parts.append(sub);
            }
        }
        else
        {
            //
            // Mixed debit transaction, can't break down payees
            //


            /** TOKENS START */
            // We will only show mixed debit transactions that are nNet < 0 or if they are nNet == 0 and
            // they do not contain tokens. This is so the list of transaction doesn't add 0 amount transactions to the
            // list.
            bool fIsMixedDebit = true;
            if (nNet == 0) {
                for (unsigned int nOut = 0; nOut < wtx.tx->vout.size(); nOut++) {
                    const CTxOut &txout = wtx.tx->vout[nOut];

                    if (txout.scriptPubKey.IsTokenScript()) {
                        fIsMixedDebit = false;
                        break;
                    }
                }
            }

            if (fIsMixedDebit) {
                parts.append(TransactionRecord(hash, nTime, TransactionRecord::Other, "", nNet, 0));
                parts.last().involvesWatchAddress = involvesWatchAddress;
            }
            /** TOKENS START */
        }
    }


    /** TOKENS START */
    if (AreTokensDeployed()) {
        CAmount nFee;
        std::string strSentAccount;
        std::list<COutputEntry> listReceived;
        std::list<COutputEntry> listSent;

        std::list<CTokenOutputEntry> listTokensReceived;
        std::list<CTokenOutputEntry> listTokensSent;

        wtx.GetAmounts(listReceived, listSent, nFee, strSentAccount, ISMINE_ALL, listTokensReceived, listTokensSent);

        if (listTokensReceived.size() > 0)
        {
            for (const CTokenOutputEntry &data : listTokensReceived)
            {
                TransactionRecord sub(hash, nTime);
                sub.idx = data.vout;

                const CTxOut& txout = wtx.tx->vout[sub.idx];
                isminetype mine = wallet->IsMine(txout);

                sub.address = EncodeDestination(data.destination);
                sub.tokenName = data.tokenName;
                sub.credit = data.nAmount;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;

                if (data.type == TX_NEW_TOKEN)
                    sub.type = TransactionRecord::Issue;
                else if (data.type == TX_REISSUE_TOKEN)
                    sub.type = TransactionRecord::Reissue;
                else if (data.type == TX_TRANSFER_TOKEN)
                    sub.type = TransactionRecord::TransferFrom;
                else {
                    sub.type = TransactionRecord::Other;
                }

                sub.units = DEFAULT_UNITS;

                if (IsTokenNameAnOwner(sub.tokenName))
                    sub.units = OWNER_UNITS;
                else if (CheckIssueDataTx(wtx.tx->vout[sub.idx]))
                {
                    CNewToken token;
                    std::string strAddress;
                    if (TokenFromTransaction(*wtx.tx, token, strAddress))
                        sub.units = token.units;
                }
                else
                {
                    CNewToken token;
                    if (ptokens->GetTokenMetaDataIfExists(sub.tokenName, token))
                        sub.units = token.units;
                }

                parts.append(sub);
            }
        }

        if (listTokensSent.size() > 0)
        {
            for (const CTokenOutputEntry &data : listTokensSent)
            {
                TransactionRecord sub(hash, nTime);
                sub.idx = data.vout;
                sub.address = EncodeDestination(data.destination);
                sub.tokenName = data.tokenName;
                sub.credit = -data.nAmount;
                sub.involvesWatchAddress = false;

                if (data.type == TX_TRANSFER_TOKEN)
                    sub.type = TransactionRecord::TransferTo;
                else
                    sub.type = TransactionRecord::Other;

                if (IsTokenNameAnOwner(sub.tokenName))
                    sub.units = OWNER_UNITS;
                else if (CheckIssueDataTx(wtx.tx->vout[sub.idx]))
                {
                    CNewToken token;
                    std::string strAddress;
                    if (TokenFromTransaction(*wtx.tx, token, strAddress))
                        sub.units = token.units;
                }
                else
                {
                    CNewToken token;
                    if (ptokens->GetTokenMetaDataIfExists(sub.tokenName, token))
                        sub.units = token.units;
                }

                parts.append(sub);
            }
        }
    }
    /** TOKENS END */

    return parts;
}

void TransactionRecord::updateStatus(const CWalletTx &wtx)
{
    AssertLockHeld(cs_main);
    // Determine transaction status

    // Find the block the tx is in
    CBlockIndex* pindex = nullptr;
    BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
    if (mi != mapBlockIndex.end())
        pindex = (*mi).second;

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        (pindex ? pindex->nHeight : std::numeric_limits<int>::max()),
        ((wtx.IsCoinBase() || wtx.IsCoinStake()) ? 1 : 0),
        wtx.nTimeReceived,
        idx);
    status.countsForBalance = wtx.IsTrusted() && !(wtx.GetBlocksToMaturity() > 0);
    status.depth = wtx.GetDepthInMainChain();
    status.cur_num_blocks = chainActive.Height();

    if (!CheckFinalTx(*wtx.tx))
    {
        if (wtx.tx->nLockTime < LOCKTIME_THRESHOLD)
        {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.tx->nLockTime - chainActive.Height();
        }
        else
        {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.tx->nLockTime;
        }
    }
    // For generated transactions, determine maturity
    else if(type == TransactionRecord::Generated)
    {
        if (wtx.GetBlocksToMaturity() > 0)
        {
            status.status = TransactionStatus::Immature;

            if (wtx.IsInMainChain())
            {
                status.matures_in = wtx.GetBlocksToMaturity();

                // Check if the block was requested by anyone
                if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                    status.status = TransactionStatus::MaturesWarning;
            }
            else
            {
                status.status = TransactionStatus::NotAccepted;
            }
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    else
    {
        if (status.depth < 0)
        {
            status.status = TransactionStatus::Conflicted;
        }
        else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
        {
            status.status = TransactionStatus::Offline;
        }
        else if (status.depth == 0)
        {
            status.status = TransactionStatus::Unconfirmed;
            if (wtx.isAbandoned())
                status.status = TransactionStatus::Abandoned;
        }
        else if (status.depth < RecommendedNumConfirmations)
        {
            status.status = TransactionStatus::Confirming;
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    status.needsUpdate = false;
}

bool TransactionRecord::statusUpdateNeeded() const
{
    AssertLockHeld(cs_main);
    return status.cur_num_blocks != chainActive.Height() || status.needsUpdate;
}

QString TransactionRecord::getTxID() const
{
    return QString::fromStdString(hash.ToString());
}

int TransactionRecord::getOutputIndex() const
{
    return idx;
}
