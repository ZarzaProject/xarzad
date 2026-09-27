// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <logging.h>

/**
 * XARZA DARK GRAVITY WAVE v3
 * Ajuste de dificultad por bloque usando promedio móvil.
 */
unsigned int DarkGravityWave(const CBlockIndex* pindexLast, const Consensus::Params& params) {
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    const int64_t nActualSpacing = params.nPowTargetSpacing;
    const int64_t nPastBlocks = 24; // Ventana de bloques para el promedio

    if (pindexLast == nullptr || pindexLast->nHeight < nPastBlocks) {
        return bnPowLimit.GetCompact();
    }

    const CBlockIndex* pindex = pindexLast;
    arith_uint256 bnPastTargetAvg;

    for (unsigned int nCountBlocks = 1; nCountBlocks <= nPastBlocks; nCountBlocks++) {
        arith_uint256 bnTarget = arith_uint256().SetCompact(pindex->nBits);
        if (nCountBlocks == 1) {
            bnPastTargetAvg = bnTarget;
        } else {
            // Promedio acumulado (Ponderado hacia el bloque más reciente)
            bnPastTargetAvg = (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);
        }

        if (nCountBlocks != nPastBlocks) {
            assert(pindex->pprev);
            pindex = pindex->pprev;
        }
    }

    arith_uint256 bnNew(bnPastTargetAvg);

    // Tiempo transcurrido real entre los bloques de la ventana
    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindex->GetBlockTime();
    int64_t nTargetTimespan = nPastBlocks * nActualSpacing;

    // Límites de seguridad: No más de 3x de subida o bajada por bloque
    if (nActualTimespan < nTargetTimespan / 3)
        nActualTimespan = nTargetTimespan / 3;
    if (nActualTimespan > nTargetTimespan * 3)
        nActualTimespan = nTargetTimespan * 3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    // El límite de dificultad (powLimit)
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Bloque Génesis
    if (pindexLast == nullptr)
        return nProofOfWorkLimit;

    // Regla especial para Testnet: Permitir bloques fáciles si no se mina en un tiempo
    if (params.fPowAllowMinDifficultyBlocks)
    {
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 2)
            return nProofOfWorkLimit;
    }

    // Ejecutar Dark Gravity Wave para el resto
    return DarkGravityWave(pindexLast, params);
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Validar rango
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Validar contra el hash calculado del bloque
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}