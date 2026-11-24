// Payload-sharing circuit-PSI front end, adapted from volePSI/RsCpsi.cpp, (c) 2022 Visa.
// MIT License: Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to
// whom the Software is furnished to do so, subject to the following conditions: The above
// copyright notice and this permission notice shall be included in all copies or substantial
// portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
//
// Differences from RsCpsi: fresh random cuckoo seed; no key-equality flag / isZero GMW. Each
// bin only gets XOR shares of the payload of the programmed key equal to the bin's query (or a
// pseudorandom string when there is none); the caller's circuit rejects garbage via its own
// statistical test (payload tag and/or distance threshold).
#include "HamCpsi.h"

#include "cryptoTools/Common/CuckooIndex.h"
#include "volePSI/RsOpprf.h"
#include "volePSI/SimpleIndex.h"

#include <cstdlib>
#include <stdexcept>

namespace mip
{
    using namespace volePSI;
    using oc::Matrix;

    namespace
    {
        // per-hash-function domain separation keys (the first three as in volePSI RsCpsi)
        block hashKey(u64 h)
        {
            static const block keys[4] = {block(3242, 23423), block(4534, 45654), block(5677, 67867), block(6789, 78978)};
            if (h >= 4)
                throw std::runtime_error("at most 4 cuckoo hash functions supported");
            return keys[h];
        }

        // volePSI's SimpleIndex::insertItems writes bins without a bounds check, so a bin above the
        // statistical size bound would silently overwrite its neighbour. We recompute the exact
        // loads with the same location function first and enlarge the bins when needed, which
        // makes simple hashing correct for every input (no failure event).
        void safeInsert(SimpleIndex& sIdx, oc::span<block> items, block seed)
        {
            oc::CuckooIndex<> cuckoo;
            cuckoo.mMods.resize(sIdx.mNumHashFunctions);
            for (u64 i = 0; i < cuckoo.mMods.size(); ++i)
                cuckoo.mMods[i] = oc::Mod(sIdx.mNumBins - i);
            std::vector<u64> load(sIdx.mNumBins, 0);
            oc::Matrix<u32> locations(32, sIdx.mNumHashFunctions);
            std::array<block, 32> hashs;
            oc::AES hasher(seed);
            for (u64 i = 0; i < items.size(); i += hashs.size())
            {
                auto min = std::min<u64>(items.size() - i, hashs.size());
                hasher.hashBlocks(items.data() + i, min, hashs.data());
                cuckoo.computeLocations(hashs, locations);
                for (u64 j = 0; j < sIdx.mNumHashFunctions; ++j)
                    for (u64 k = 0; k < min; ++k)
                        ++load[locations(k, j)];
            }
            u64 maxLoad = *std::max_element(load.begin(), load.end());
            if (maxLoad > sIdx.mMaxBinSize)
            {
                sIdx.mMaxBinSize = maxLoad;
                sIdx.mBins.resize(sIdx.mNumBins, maxLoad);
            }
            sIdx.insertItems(items, seed);
            for (u64 i = 0; i < sIdx.mNumBins; ++i)
                if (sIdx.mBinSizes[i] != load[i])
                    throw std::runtime_error("simple hashing: load mismatch");
        }
    }

    Proto HamCpsiSender::send(oc::span<block> Y, oc::MatrixView<std::uint8_t> values, Sharing& ret, Socket& chl)
    {
        auto cuckooSeed = block{};
        auto params = oc::CuckooParam{};
        auto numBins = u64{};
        auto sIdx = SimpleIndex{};
        auto hashers = std::vector<oc::AES>(mNumHash);
        auto Ty = std::vector<block>{};
        auto Tv = Matrix<std::uint8_t>{};
        auto opprf = std::make_unique<RsOpprfSender>();

        if (mSenderSize != Y.size() || mValueByteLength != values.cols())
            throw RTE_LOC;

        co_await chl.recv(cuckooSeed);

        params = oc::CuckooIndex<>::selectParams(mRecverSize, mSsp, 0, mNumHash);
        numBins = params.numBins();
        sIdx.init(numBins, mSenderSize, mSsp, mNumHash);
        safeInsert(sIdx, Y, cuckooSeed);

        for (u64 h = 0; h < mNumHash; ++h)
            hashers[h].setKey(hashKey(h) ^ cuckooSeed);

        Ty.resize(Y.size() * mNumHash);
        Tv.resize(Y.size() * mNumHash, values.cols(), oc::AllocType::Uninitialized);
        ret.mValues.resize(numBins, values.cols(), oc::AllocType::Uninitialized);
        mPrng.get<std::uint8_t>(ret.mValues);

        {
            u64 k = 0;
            for (u64 i = 0; i < numBins; ++i)
            {
                auto bin = sIdx.mBins[i];
                auto size = sIdx.mBinSizes[i];
                for (u64 p = 0; p < size; ++p, ++k)
                {
                    auto j = bin[p].hashIdx();
                    auto b = bin[p].idx();
                    Ty[k] = hashers[j].hashBlock(Y[b]);
                    for (u64 c = 0; c < values.cols(); ++c)
                        Tv(k, c) = values(b, c) ^ ret.mValues(i, c);
                }
            }
            for (; k < Ty.size(); ++k)
            {
                Ty[k] = mPrng.get();
                mPrng.get(&Tv(k, 0), values.cols());
            }
        }

        co_await opprf->send(numBins, Ty, Tv, mPrng, mNumThreads, chl);
    }

    Proto HamCpsiReceiver::receive(oc::span<block> X, Sharing& ret, Socket& chl)
    {
        auto cuckooSeed = block{};
        auto cuckooPtr = std::unique_ptr<oc::CuckooIndex<>>{};
        auto Tx = std::vector<block>{};
        auto hashers = std::vector<oc::AES>(mNumHash);
        auto numBins = u64{};
        auto opprf = std::make_unique<RsOpprfReceiver>();

        if (mRecverSize != X.size())
            throw RTE_LOC;

        // Cuckoo failure (prob. <= 2^-ssp) depends only on the seed and on the queries, which are
        // random-oracle outputs, so a fresh seed is drawn until all queries are placed.
        for (u64 attempt = 0;; ++attempt)
        {
            if (attempt == 16)
                throw std::runtime_error("cuckoo hashing failed 16 times");
            cuckooSeed = mPrng.get();
            cuckooPtr = std::make_unique<oc::CuckooIndex<>>();
            cuckooPtr->init(mRecverSize, mSsp, 0, mNumHash);
            try
            {
                // Test hook: MIP_INJECT_CUCKOO_FAIL=k makes the first k insertions fail.
                if (const char* inj = std::getenv("MIP_INJECT_CUCKOO_FAIL"); inj && attempt < std::strtoull(inj, nullptr, 10))
                    throw std::runtime_error("injected cuckoo failure");
                cuckooPtr->insert(X, cuckooSeed);
            }
            catch (const std::exception&)
            {
                continue; // stash overflow: resample the seed locally
            }
            u64 placed = 0;
            for (auto& bin : cuckooPtr->mBins)
                placed += !bin.isEmpty();
            if (placed == X.size())
                break;
        }
        {
            auto seedCopy = cuckooSeed;
            co_await chl.send(std::move(seedCopy));
        }
        Tx.resize(cuckooPtr->mNumBins);

        for (u64 h = 0; h < mNumHash; ++h)
            hashers[h].setKey(hashKey(h) ^ cuckooSeed);

        ret.mMapping.resize(X.size(), ~u64(0));
        numBins = cuckooPtr->mBins.size();
        for (u64 i = 0; i < numBins; ++i)
        {
            auto& bin = cuckooPtr->mBins[i];
            if (bin.isEmpty() == false)
            {
                auto j = bin.hashIdx();
                auto b = bin.idx();
                Tx[i] = hashers[j].hashBlock(X[b]);
                ret.mMapping[b] = i;
            }
            else
                Tx[i] = block(i, 0);
        }
        for (auto v : ret.mMapping)
            if (v == ~u64(0))
                throw std::runtime_error("cuckoo insertion failed (stash not supported)");

        ret.mValues.resize(Tx.size(), mValueByteLength, oc::AllocType::Uninitialized);
        co_await opprf->receive(mSenderSize * mNumHash, Tx, ret.mValues, mPrng, mNumThreads, chl);
    }
}
