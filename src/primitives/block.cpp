// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <crypto/common.h>
#include <crypto/scrypt.h>
#include <streams.h>
#include <consensus/consensus.h>

void ReadBlockWithAuth(CDataStream& stream, CBlock& block)
{
    if (stream.size() > MAX_BLOCK_SERIALIZED_SIZE) throw std::ios_base::failure("Block record too large");
    stream >> block;
    if (stream.empty()) return;
    uint32_t magic;
    stream >> magic;
    if (magic != MINING_AUTH_MAGIC) throw std::ios_base::failure("Unknown mining authorization format");
    const uint64_t size = ReadCompactSize(stream);
    if (size < 8 || size > MAX_MINING_SIGNATURE_SIZE || stream.size() != size) {
        throw std::ios_base::failure("Invalid mining authorization length");
    }
    block.vchMiningSignature.resize(size);
    stream.read(reinterpret_cast<char*>(block.vchMiningSignature.data()), size);
}

uint256 CBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CBlockHeader::GetPoWHash() const
{
    uint256 thash;
    scrypt_1024_1_1_256(BEGIN(nVersion), BEGIN(thash));
    return thash;
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
