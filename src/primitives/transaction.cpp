// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/transaction.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"

std::string COutPoint::ToString() const
{
    return strprintf("COutPoint(%s, %u)", hash.ToString().substr(0,10), n);
}

CTxIn::CTxIn(COutPoint prevoutIn, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = prevoutIn;
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

CTxIn::CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = COutPoint(hashPrevTx, nOut);
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

std::string CTxIn::ToString() const
{
    std::string str;
    str += "CTxIn(";
    str += prevout.ToString();
    if (prevout.IsNull())
        str += strprintf(", coinbase %s", HexStr(scriptSig));
    else
        str += strprintf(", scriptSig=%s", HexStr(scriptSig).substr(0, 24));
    if (nSequence != SEQUENCE_FINAL)
        str += strprintf(", nSequence=%u", nSequence);
    str += ")";
    return str;
}


CTxOutValue::CTxOutValue()
{
    vchCommitment.resize(nCommitmentSize);
    vchCommitment[0] = 0xff;
}

CTxOutValue::CTxOutValue(CAmount nAmountIn)
{
    vchCommitment.resize(nCommitmentSize);
    SetToAmount(nAmountIn);
}

bool CTxOutValue::IsValid() const
{
    switch(vchCommitment[0]) {
        case 0:
        case 1:
            for (size_t i = 0; i < nCommitmentSize - sizeof(CAmount); i++)
                if (vchCommitment[i])
                    return false;
            return true;
        case 2:
        case 3:
            return true;
        default:
            return false;
    }
}

bool CTxOutValue::IsNull() const
{
    return vchCommitment[0] == 0xff;
}

bool CTxOutValue::IsAmount() const
{
    return vchCommitment[0] == 0 || vchCommitment[0] == 1;
}

CAmount CTxOutValue::GetAmount() const
{
    assert(IsAmount());
    CAmount nAmount = 0;
    for (size_t i = 0; i < sizeof(nAmount); i++)
        nAmount |= CAmount(vchCommitment[nCommitmentSize - 1 - i]) << (i * 8);
    return nAmount;
}

bool operator==(const CTxOutValue& a, const CTxOutValue& b)
{
    return a.vchRangeproof == b.vchRangeproof &&
           a.vchCommitment == b.vchCommitment &&
           a.vchNonceCommitment == b.vchNonceCommitment;
}

bool operator!=(const CTxOutValue& a, const CTxOutValue& b) {
    return !(a == b);
}

void CTxOutValue::SetToBitcoinAmount(const CAmount nAmount) {
    SetToAmount(nAmount);
    vchCommitment[0] = 1;
}

bool CTxOutValue::IsInBitcoinTransaction() const {
    return vchCommitment[0] == 1;
}

void CTxOutValue::SetToAmount(const CAmount nAmount) {
    memset(&vchCommitment[0], 0, nCommitmentSize - sizeof(nAmount));
    for (size_t i = 0; i < sizeof(nAmount); ++i)
        vchCommitment[nCommitmentSize - 1 - i] = ((nAmount >> (i * 8)) & 0xff);
}

CTxOut::CTxOut(const CTxOutValue& nValueIn, CScript scriptPubKeyIn)
{
    nValue = nValueIn;
    scriptPubKey = scriptPubKeyIn;
}

std::string CTxOut::ToString() const
{
    return strprintf("CTxOut(nValue=%s, scriptPubKey=%s)", (nValue.IsAmount() ? strprintf("%d.%08d", nValue.GetAmount() / COIN, nValue.GetAmount() % COIN) : std::string("UNKNOWN")), HexStr(scriptPubKey).substr(0, 30));
}

CMutableTransaction::CMutableTransaction() : nVersion(CTransaction::CURRENT_VERSION), nTxFee(0), nLockTime(0) {}
CMutableTransaction::CMutableTransaction(const CTransaction& tx) : nVersion(tx.nVersion), nTxFee(tx.nTxFee), vin(tx.vin), vout(tx.vout), wit(tx.wit), nLockTime(tx.nLockTime) {}

uint256 CMutableTransaction::GetHash() const
{
    return SerializeHash(*this, SER_GETHASH, SERIALIZE_TRANSACTION_NO_WITNESS);
}

void CTransaction::UpdateHash() const
{
    *const_cast<uint256*>(&hash) = SerializeHash(*this, SER_GETHASH, SERIALIZE_TRANSACTION_NO_WITNESS);
}

uint256 CTransaction::GetWitnessHash() const
{
    return SerializeHash(*this, SER_GETHASH, 0);
}

CTransaction::CTransaction() : nVersion(CTransaction::CURRENT_VERSION), nTxFee(0), vin(), vout(), nLockTime(0) { }

CTransaction::CTransaction(const CMutableTransaction &tx) : nVersion(tx.nVersion), nTxFee(tx.nTxFee), vin(tx.vin), vout(tx.vout), wit(tx.wit), nLockTime(tx.nLockTime) {
    UpdateHash();
}

CTransaction& CTransaction::operator=(const CTransaction &tx) {
    *const_cast<int*>(&nVersion) = tx.nVersion;
    *const_cast<CAmount*>(&nTxFee) = tx.nTxFee;
    *const_cast<std::vector<CTxIn>*>(&vin) = tx.vin;
    *const_cast<std::vector<CTxOut>*>(&vout) = tx.vout;
    *const_cast<CTxWitness*>(&wit) = tx.wit;
    *const_cast<unsigned int*>(&nLockTime) = tx.nLockTime;
    *const_cast<uint256*>(&hash) = tx.hash;
    return *this;
}

double CTransaction::ComputePriority(double dPriorityInputs, unsigned int nTxSize) const
{
    nTxSize = CalculateModifiedSize(nTxSize);
    if (nTxSize == 0) return 0.0;

    return dPriorityInputs / nTxSize;
}

unsigned int CTransaction::CalculateModifiedSize(unsigned int nTxSize) const
{
    // In order to avoid disincentivizing cleaning up the UTXO set we don't count
    // the constant overhead for each txin and up to 110 bytes of scriptSig (which
    // is enough to cover a compressed pubkey p2sh redemption) for priority.
    // Providing any more cleanup incentive than making additional inputs free would
    // risk encouraging people to create junk outputs to redeem later.
    if (nTxSize == 0)
        nTxSize = (GetTransactionWeight(*this) + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR;
    for (std::vector<CTxIn>::const_iterator it(vin.begin()); it != vin.end(); ++it)
    {
        unsigned int offset = 41U + std::min(110U, (unsigned int)it->scriptSig.size());
        if (nTxSize > offset)
            nTxSize -= offset;
    }
    return nTxSize;
}

std::string CTransaction::ToString() const
{
    std::string str;
    str += strprintf("CTransaction(hash=%s, ver=%d, fee=%d.%08d, vin.size=%u, vout.size=%u, nLockTime=%u)\n",
        GetHash().ToString().substr(0,10),
        nVersion,
        nTxFee / COIN, nTxFee % COIN,
        vin.size(),
        vout.size(),
        nLockTime);
    for (unsigned int i = 0; i < vin.size(); i++)
        str += "    " + vin[i].ToString() + "\n";
    for (unsigned int i = 0; i < wit.vtxinwit.size(); i++)
        str += "    " + wit.vtxinwit[i].scriptWitness.ToString() + "\n";
    for (unsigned int i = 0; i < vout.size(); i++)
        str += "    " + vout[i].ToString() + "\n";
    return str;
}

int64_t GetTransactionWeight(const CTransaction& tx)
{
    return ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * (WITNESS_SCALE_FACTOR -1) + ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
}
