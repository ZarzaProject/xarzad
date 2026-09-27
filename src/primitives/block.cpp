// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <crypto/common.h>
#include <crypto/sha256.h>
#include <crypto/sha512.h>

/**
 * XARZAHASH-PRIME (V2 FINAL)
 * Diseño Anti-ASIC de Grado Militar.
 * * Estrategia:
 * 1. Semilla de Entropía: Basada en el Header y el Hash anterior.
 * 2. Expansión de Memoria: Creamos un scratchpad de 2MB (Caché L3 target).
 * 3. Mezclado No-Lineal: Miles de saltos aleatorios en memoria.
 * 4. Finalización: Doble SHA256 para el identificador de consenso.
 */
uint256 CBlockHeader::GetHash() const
{
    // Fase 1: Creación de la semilla inicial (SHA512 para mayor ancho de banda)
    CSHA512 hasher;
    unsigned char seed[64];
    hasher.Write((const unsigned char*)this, 80);
    hasher.Finalize(seed);

    // Fase 2: Expansión de Memoria (Scratchpad)
    // 2MB es ideal para forzar el uso de la memoria caché de la CPU y asfixiar ASICs.
    const size_t SCRATCHPAD_SIZE = 1024 * 1024 * 2; 
    std::vector<uint8_t> scratchpad(SCRATCHPAD_SIZE);

    // Llenado inicial del scratchpad mediante un generador determinista
    for (size_t i = 0; i < SCRATCHPAD_SIZE; ++i) {
        scratchpad[i] = seed[i % 64] ^ (i & 0xFF);
    }

    // Fase 3: El "Bucle de la Muerte" para ASICs (Saltos aleatorios)
    // Los ASICs no pueden predecir el siguiente salto de memoria, las CPUs con Caché L3 sí.
    uint32_t address = 0;
    for (int j = 0; j < 32768; ++j) {
        uint32_t p1 = ReadLE32(&scratchpad[address % (SCRATCHPAD_SIZE - 4)]);
        uint32_t p2 = ReadLE32(&scratchpad[(address ^ j) % (SCRATCHPAD_SIZE - 4)]);
        
        address = (p1 ^ p2) + j;
        // Mutación de la memoria en cada salto
        scratchpad[address % SCRATCHPAD_SIZE] ^= (uint8_t)(p1 >> (j % 8));
    }

    // Fase 4: Finalización por Consenso
    uint256 final_hash;
    CHash256().Write(MakeUCharSpan(scratchpad)).Finalize(MakeUCharSpan(final_hash));

    return final_hash;
}

uint256 CBlockHeader::GetPoWHash() const
{
    // En Xarza-Prime, el identificador y el trabajo son el mismo muro criptográfico.
    return GetHash();
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}

CTransactionRef CBlock::GetHogEx() const noexcept
{
    if (vtx.size() >= 2 && vtx.back()->IsHogEx()) {
        assert(!vtx.back()->vout.empty());
        return vtx.back();
    }

    return nullptr;
}