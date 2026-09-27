// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <miner.h>

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <mw/consensus/Params.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <timedata.h>
#include <util/moneystr.h>
#include <util/system.h>
#include <util/strencodings.h>

#include <algorithm>
#include <utility>

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Actualizar el tiempo puede cambiar el trabajo requerido en testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks)
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);

    return nNewTime - nOldTime;
}

void RegenerateCommitments(CBlock& block)
{
    CMutableTransaction tx{*block.vtx.at(0)};
    tx.vout.erase(tx.vout.begin() + GetWitnessCommitmentIndex(block));
    block.vtx.at(0) = MakeTransactionRef(tx);

    GenerateCoinbaseCommitment(block, WITH_LOCK(cs_main, return LookupBlockIndex(block.hashPrevBlock)), Params().GetConsensus());

    block.hashMerkleRoot = BlockMerkleRoot(block);
}

BlockAssembler::Options::Options() {
    blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    nBlockMaxWeight = DEFAULT_BLOCK_MAX_WEIGHT;
}

BlockAssembler::BlockAssembler(const CTxMemPool& mempool, const CChainParams& params, const Options& options)
    : chainparams(params),
      m_mempool(mempool)
{
    blockMinFeeRate = options.blockMinFeeRate;
    nBlockMaxWeight = std::max<size_t>(4000, std::min<size_t>(MAX_BLOCK_WEIGHT - 4000, options.nBlockMaxWeight));
}

static BlockAssembler::Options DefaultOptions()
{
    BlockAssembler::Options options;
    options.nBlockMaxWeight = gArgs.GetArg("-blockmaxweight", DEFAULT_BLOCK_MAX_WEIGHT);
    CAmount n = 0;
    if (gArgs.IsArgSet("-blockmintxfee") && ParseMoney(gArgs.GetArg("-blockmintxfee", ""), n)) {
        options.blockMinFeeRate = CFeeRate(n);
    } else {
        options.blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    }
    return options;
}

BlockAssembler::BlockAssembler(const CTxMemPool& mempool, const CChainParams& params)
    : BlockAssembler(mempool, params, DefaultOptions()) {}

void BlockAssembler::resetBlock()
{
    inBlock.clear();
    nBlockWeight = 4000;
    nBlockSigOpsCost = 400;
    nBlockMWEBWeight = 0;
    fIncludeWitness = false;
    fIncludeMWEB = false;
    nBlockTx = 0;
    nFees = 0;
}

Optional<int64_t> BlockAssembler::m_last_block_num_txs{nullopt};
Optional<int64_t> BlockAssembler::m_last_block_weight{nullopt};
Optional<int64_t> BlockAssembler::m_last_block_mweb_weight{nullopt};

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn)
{
    int64_t nTimeStart = GetTimeMicros();

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if(!pblocktemplate.get())
        return nullptr;
    CBlock* const pblock = &pblocktemplate->block;

    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1); 
    pblocktemplate->vTxSigOpsCost.push_back(-1); 

    LOCK2(cs_main, m_mempool.cs);
    CBlockIndex* pindexPrev = ::ChainActive().Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    pblock->nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
    if (chainparams.MineBlocksOnDemand())
        pblock->nVersion = gArgs.GetArg("-blockversion", pblock->nVersion);

    pblock->nTime = GetAdjustedTime();
    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                       ? nMedianTimePast
                       : pblock->GetBlockTime();

    fIncludeWitness = IsWitnessEnabled(pindexPrev, chainparams.GetConsensus());

    fIncludeMWEB = IsMWEBEnabled(pindexPrev, chainparams.GetConsensus());
    if (fIncludeMWEB) {
        mweb_miner.NewBlock(nHeight);
    }

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    addPackageTxs(nPackagesSelected, nDescendantsUpdated);

    if (fIncludeMWEB) {
        mweb_miner.AddHogExTransaction(pindexPrev, pblock, pblocktemplate.get(), nFees);
    }

    int64_t nTime1 = GetTimeMicros();

    m_last_block_num_txs = nBlockTx;
    m_last_block_weight = nBlockWeight;
    m_last_block_mweb_weight = nBlockMWEBWeight;

    // --- SECCIÓN MODIFICADA PARA XARZA ECONOMY (XARZA-PRIME) ---
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();

    // 1. Obtener la recompensa base (7.45 XRZ inicial)
    CAmount nBlockSubsidy = GetBlockSubsidy(nHeight, chainparams.GetConsensus());
    
    // 2. Lógica de reparto con identificación clara
    if (nHeight > 10) {
        // A partir del bloque 11: 90% Minero / 10% Dev Fee
        CAmount nDevFee = nBlockSubsidy / 10;
        CAmount nMinerReward = nBlockSubsidy - nDevFee;

        coinbaseTx.vout.resize(2);

        // Salida 0: Pago al Minero
        coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;
        coinbaseTx.vout[0].nValue = nMinerReward;

        // Salida 1: Pago al Desarrollador Pablo (xrz1qslsddk8ga4ttkcfar5xuu9azyck2r4x0x4mwgz)
        coinbaseTx.vout[1].scriptPubKey = CScript() << OP_0 << ParseHex("0b18fe3e24e8a160b609ac0edfb93a80237b4825");
        coinbaseTx.vout[1].nValue = nDevFee;
    } else {
        // Bloques 1 al 10: 100% para el minero
        coinbaseTx.vout.resize(1);
        coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;
        coinbaseTx.vout[0].nValue = nBlockSubsidy;
    }

    // Identificador único en el scriptSig para marcar el protocolo Xarza-Prime
    coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0 << ToByteVector(std::string("XarzaPrime-AntiASIC-Active"));
    
    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));

    // 3. Generar compromisos para SegWit
    if (fIncludeWitness) {
        GenerateCoinbaseCommitment(*pblock, pindexPrev, chainparams.GetConsensus());
    }

    // 4. Quema de comisiones: No las sumamos a la coinbase.
    // El valor negativo indica al template que se queman.
    pblocktemplate->vTxFees[0] = -nFees; 
    // --- FIN SECCIÓN MODIFICADA ---

    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
    pblock->nNonce         = 0; // El minero externo buscará este valor
    
    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    BlockValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, state.ToString()));
    }
    int64_t nTime2 = GetTimeMicros();

    LogPrint(BCLog::BENCH, "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n", 0.001 * (nTime1 - nTimeStart), nPackagesSelected, nDescendantsUpdated, 0.001 * (nTime2 - nTime1), 0.001 * (nTime2 - nTimeStart));

    return std::move(pblocktemplate);
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        }
        else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, int64_t packageSigOpsCost, int64_t packageMWEBWeight) const
{
    if (nBlockWeight + WITNESS_SCALE_FACTOR * packageSize >= nBlockMaxWeight)
        return false;
    if (nBlockSigOpsCost + packageSigOpsCost >= MAX_BLOCK_SIGOPS_COST)
        return false;
    if (nBlockMWEBWeight + packageMWEBWeight >= mw::MAX_MINE_WEIGHT)
        return false;
    return true;
}

bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package, const CTxMemPool::setEntries& failedTxs)
{
    for (CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, nLockTimeCutoff))
            return false;
        if (!fIncludeWitness && it->GetTx().HasWitness())
            return false;
        if (!fIncludeMWEB && it->GetTx().HasMWEBTx()) {
            return false;
        }
        if (failedTxs.count(it) > 0) {
            return false;
        }
    }
    return true;
}

bool BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    if (iter->GetTx().HasMWEBTx() && !mweb_miner.AddMWEBTransaction(iter)) {
        return false;
    }

    CTransactionRef pTx = iter->GetSharedTx();
    if (!pTx->IsMWEBOnly()) {
        if (pTx->HasMWEBTx()) {
            CMutableTransaction mutable_tx(*pTx);
            mutable_tx.mweb_tx.SetNull();
            pTx = MakeTransactionRef(std::move(mutable_tx));
        }

        pblocktemplate->block.vtx.emplace_back(pTx);
        pblocktemplate->vTxFees.push_back(iter->GetFee());
        pblocktemplate->vTxSigOpsCost.push_back(iter->GetSigOpCost());
        ++nBlockTx;
    }

    nBlockWeight += iter->GetTxWeight();
    nBlockSigOpsCost += iter->GetSigOpCost();
    nBlockMWEBWeight += iter->GetMWEBWeight();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = gArgs.GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        LogPrintf("fee %s txid %s\n",
                  CFeeRate(iter->GetModifiedFee(), iter->GetTxSize(), iter->GetMWEBWeight()).ToString(),
                  iter->GetTx().GetHash().ToString());
    }

    return true;
}

int BlockAssembler::UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded,
        indexed_modified_transaction_set &mapModifiedTx)
{
    int nDescendantsUpdated = 0;
    for (CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        m_mempool.CalculateDescendants(it, descendants);
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc))
                continue;
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                modEntry.nSizeWithAncestors -= it->GetTxSize();
                modEntry.nModFeesWithAncestors -= it->GetModifiedFee();
                modEntry.nSigOpCostWithAncestors -= it->GetSigOpCost();
                modEntry.nMWEBWeightWithAncestors -= it->GetMWEBWeight();
                mapModifiedTx.insert(modEntry);
            } else {
                mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
            }
        }
    }
    return nDescendantsUpdated;
}

bool BlockAssembler::SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set &mapModifiedTx, CTxMemPool::setEntries &failedTx)
{
    assert(it != m_mempool.mapTx.end());
    return mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it);
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

void BlockAssembler::addPackageTxs(int &nPackagesSelected, int &nDescendantsUpdated)
{
    indexed_modified_transaction_set mapModifiedTx;
    CTxMemPool::setEntries failedTx;

    UpdatePackagesForAdded(inBlock, mapModifiedTx);

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = m_mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != m_mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty()) {
        if (mi != m_mempool.mapTx.get<ancestor_score>().end() &&
            SkipMapTxEntry(m_mempool.mapTx.project<0>(mi), mapModifiedTx, failedTx)) {
            ++mi;
            continue;
        }

        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == m_mempool.mapTx.get<ancestor_score>().end()) {
            iter = modit->iter;
            fUsingModified = true;
        } else {
            iter = m_mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                    CompareTxMemPoolEntryByAncestorFee()(*modit, CTxMemPoolModifiedEntry(iter))) {
                iter = modit->iter;
                fUsingModified = true;
            } else {
                ++mi;
            }
        }

        assert(!inBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        int64_t packageSigOpsCost = iter->GetSigOpCostWithAncestors();
        int64_t packageMWEBWeight = iter->GetMWEBWeightWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOpsCost = modit->nSigOpCostWithAncestors;
            packageMWEBWeight = modit->nMWEBWeightWithAncestors;
        }

        if (packageFees < blockMinFeeRate.GetTotalFee(packageSize, packageMWEBWeight)) {
            return;
        }

        if (!TestPackage(packageSize, packageSigOpsCost, packageMWEBWeight)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            ++nConsecutiveFailed;
            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight > nBlockMaxWeight - 4000) {
                break;
            }
            continue;
        }

        CTxMemPool::setEntries ancestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        m_mempool.CalculateMemPoolAncestors(*iter, ancestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        if (!TestPackageTransactions(ancestors, failedTx)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        nConsecutiveFailed = 0;

        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, sortedEntries);

        bool failed = false;
        for (size_t i=0; i<sortedEntries.size(); ++i) {
            failed = !AddToBlock(sortedEntries[i]);
            if (failed) {
                for (size_t j = i; j < sortedEntries.size(); j++) {
                    failedTx.insert(sortedEntries[j]);
                    mapModifiedTx.erase(sortedEntries[j]);
                }
                break;
            }
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        if (!failed) {
            nDescendantsUpdated += UpdatePackagesForAdded(ancestors, mapModifiedTx);
        }
    }
}

void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight+1;
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    // Mantenemos la firma personalizada también al incrementar el ExtraNonce
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce) << ToByteVector(std::string("XarzaPrime-AntiASIC-Active")));
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}