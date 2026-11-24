#include "threshold_comparison.h"

#include "../util/ot.h"
#include "../util/prf.h"

#include "libOTe/Tools/Coproto.h"
#include "coproto/Socket/AsioSocket.h"

#include "cryptoTools/Common/Matrix.h"
#include "libOTe/Base/BaseOT.h"

#define CORRELATION_BATCH_SIZE ((size_t)2048)
#define OT_BATCH_SIZE ((size_t)4096 * 4096 / 2)

std::optional<BitVector> runNpThresholdComparisonSender(const config &cfg, coproto::Socket &chl, const std::vector<uint16_t> &distance_masks, size_t &total_communication)
{
    assert(distance_masks.size() == cfg.threshold_comparison_count);

    PRNG prng(osuCrypto::sysRandomSeed());
    Timer sender_timer;
    sender_timer.setTimePoint("start");

    size_t ot_batch_count = (cfg.threshold_comparison_count + OT_BATCH_SIZE - 1) / OT_BATCH_SIZE;

    BitVector result_shares;
    if (cfg.secret_shared)
    {
        result_shares.resize(cfg.threshold_comparison_count);
        result_shares.randomize(prng);
    }
    std::unique_ptr<osuCrypto::OtExtSender> sender = nullptr;

    for (size_t ot_batch_index = 0; ot_batch_index < ot_batch_count; ++ot_batch_index)
    {
        std::cout << "[Querier] OT Batch: " << ot_batch_index << "/" << ot_batch_count << std::endl;
        size_t ot_batch_size = std::min(cfg.threshold_comparison_count - ot_batch_index * OT_BATCH_SIZE, OT_BATCH_SIZE);
        size_t num_ots = ot_batch_size * cfg.p_bits;

        if (sender == nullptr || cfg.ot == VAL_OT_SILENT) // If we are using Silent OT, we need to reconfigure the sender for each batch.
        {
            sender = getOTExtSender(cfg, num_ots);

            osuCrypto::DefaultBaseOT base;
            osuCrypto::BitVector bv(sender->baseOtCount());
            std::vector<block> baseMsg(sender->baseOtCount());
            bv.randomize(prng);

            osuCrypto::cp::sync_wait(base.receive(bv, baseMsg, prng, chl));
            sender->setBaseOts(baseMsg, bv);

            sender_timer.setTimePoint("Base OTs");
        }

        // construct a vector to stored the random send messages.
        AlignedUnVector<std::array<block, 2>> sMsgs(num_ots);

        // perform the OTs and write the random OTs to msgs.
        osuCrypto::cp::sync_wait(sender->send(sMsgs, prng, chl));

        sender_timer.setTimePoint("Random OTs");

        // compute the sender messages
        uint32_t mask_size = ceil_prf_size(cfg, cfg.modulus);
        std::vector<BitVector> prf_expanded_masks(cfg.p_bits * 2, BitVector(mask_size));

        size_t correlation_batch_size = std::min(ot_batch_size, CORRELATION_BATCH_SIZE);
        size_t correlation_batch_count = (ot_batch_size + correlation_batch_size - 1) / correlation_batch_size;
        BitVector masked_results(correlation_batch_size * cfg.modulus);

        for (size_t batch_index = 0; batch_index < correlation_batch_count; ++batch_index)
        {
            size_t start_index = batch_index * correlation_batch_size;
            size_t end_index = std::min(start_index + correlation_batch_size, ot_batch_size);

            for (size_t comparison_index = start_index; comparison_index < end_index; ++comparison_index)
            {
                for (size_t bitpos = 0; bitpos < cfg.p_bits; ++bitpos)
                {
                    size_t ot_index = comparison_index * cfg.p_bits + bitpos;
                    prf(cfg, sMsgs[ot_index][0].data(), mask_size, prf_expanded_masks[bitpos * 2 + 0].data());
                    prf(cfg, sMsgs[ot_index][1].data(), mask_size, prf_expanded_masks[bitpos * 2 + 1].data());
                }

                size_t actual_comparison_index = ot_batch_index * OT_BATCH_SIZE + comparison_index;
                uint8_t result_share_mask = 0;
                if (cfg.secret_shared)
                {
                    result_share_mask = result_shares[actual_comparison_index];
                }

                for (int j = 0; j < cfg.modulus; ++j)
                {
                    int distance = MOD(j - (int)distance_masks[actual_comparison_index], cfg.modulus);

                    size_t result_index = (comparison_index - start_index) * cfg.modulus + j;
                    masked_results[result_index] = (distance <= ((int)cfg.threshold)) ^ result_share_mask;

                    for (size_t bitpos = 0; bitpos < cfg.p_bits; ++bitpos)
                    {
                        uint16_t index_bit = (j >> bitpos) & 1;
                        masked_results[result_index] ^= prf_expanded_masks[bitpos * 2 + index_bit][j];
                    }
                }
            }
            osuCrypto::cp::sync_wait(chl.send(masked_results));
        }
    }

    // make sure all messages have been sent.
    osuCrypto::cp::sync_wait(chl.flush());

    sender_timer.setTimePoint("Message masking and transfer");

    auto com = chl.bytesReceived() + chl.bytesSent();
    std::cout << "############ Sender Summary ############" << std::endl;
    std::cout << sender_timer << std::endl;
    std::cout << "Communication: " << com << " bytes (" << com / (1024.0 * 1024.0 * 1024.0) << " GB)" << std::endl;

    total_communication += com;
    if (cfg.secret_shared)
    {
        return result_shares;
    }
    else
    {
        return std::nullopt; // No result shares to return
    }
}

BitVector computeThresholdComparisonReceiverChoices(const std::vector<uint16_t> &maskedDistances, uint16_t p_bits, size_t start_index, size_t end_index)
{
    BitVector choices((end_index - start_index) * p_bits);
    for (size_t i = start_index; i < end_index; ++i)
    {
        for (size_t j = 0; j < p_bits; ++j)
        {
            uint8_t index_bit = (maskedDistances[i] >> j) & 1;
            choices[(i - start_index) * p_bits + j] = index_bit;
        }
    }
    return choices;
}

BitVector runNpThresholdComparisonReceiver(const config &cfg, coproto::Socket &chl, const std::vector<uint16_t> &maskedDistances)
{
    assert(maskedDistances.size() == cfg.threshold_comparison_count);

    PRNG prng(osuCrypto::sysRandomSeed());

    Timer receiver_timer;
    receiver_timer.setTimePoint("start");
    size_t ot_batch_count = (cfg.threshold_comparison_count + OT_BATCH_SIZE - 1) / OT_BATCH_SIZE;

    BitVector results(maskedDistances.size());
    std::unique_ptr<osuCrypto::OtExtReceiver> receiver = nullptr;

    for (size_t ot_batch_index = 0; ot_batch_index < ot_batch_count; ++ot_batch_index)
    {
        size_t ot_batch_size = std::min(cfg.threshold_comparison_count - ot_batch_index * OT_BATCH_SIZE, OT_BATCH_SIZE);
        size_t num_ots = ot_batch_size * cfg.p_bits;

        if (receiver == nullptr || cfg.ot == VAL_OT_SILENT) // If we are using Silent OT, we need to reconfigure the receiver for each batch.
        {
            receiver = getOTExtReceiver(cfg, num_ots);

            osuCrypto::DefaultBaseOT base;
            std::vector<std::array<block, 2>> baseMsg(receiver->baseOtCount());

            // perform the base To, call sync_wait to block until they have completed.
            osuCrypto::cp::sync_wait(base.send(baseMsg, prng, chl));
            receiver->setBaseOts(baseMsg);

            receiver_timer.setTimePoint("Base OTs");
        }

        auto choices = computeThresholdComparisonReceiverChoices(maskedDistances, cfg.p_bits, ot_batch_index * OT_BATCH_SIZE, ot_batch_index * OT_BATCH_SIZE + ot_batch_size);
        assert(choices.size() == num_ots);
        receiver_timer.setTimePoint("Compute Choices");

        // construct a vector to stored the received messages.
        AlignedUnVector<block> rMsgs(num_ots);

        // perform chosen message OTs, the results will be written to msgs.
        osuCrypto::cp::sync_wait(receiver->receive(choices, rMsgs, prng, chl));
        receiver_timer.setTimePoint("Random OTs");

        uint32_t mask_size = ceil_prf_size(cfg, cfg.modulus);
        std::vector<BitVector> prf_expanded_masks(cfg.p_bits, BitVector(mask_size));

        size_t correlation_batch_size = std::min(ot_batch_size, CORRELATION_BATCH_SIZE);
        size_t correlation_batch_count = (ot_batch_size + correlation_batch_size - 1) / correlation_batch_size;
        BitVector masked_results(correlation_batch_size * cfg.modulus);

        for (size_t batch_index = 0; batch_index < correlation_batch_count; ++batch_index)
        {
            osuCrypto::cp::sync_wait(chl.recv(masked_results));

            size_t start_index = batch_index * correlation_batch_size;
            size_t end_index = std::min(start_index + correlation_batch_size, ot_batch_size);

            for (size_t comparison_index = start_index; comparison_index < end_index; ++comparison_index)
            {
                for (size_t bitpos = 0; bitpos < cfg.p_bits; ++bitpos)
                {
                    size_t ot_index = comparison_index * cfg.p_bits + bitpos;
                    prf(cfg, rMsgs[ot_index].data(), mask_size, prf_expanded_masks[bitpos].data());
                }
                size_t actual_comparison_index = ot_batch_index * OT_BATCH_SIZE + comparison_index;
                results[actual_comparison_index] = masked_results[(comparison_index - start_index) * cfg.modulus + maskedDistances[actual_comparison_index]];
                
                // Unmask the result using the prf-extended OT messages
                for (size_t bitpos = 0; bitpos < cfg.p_bits; ++bitpos)
                {
                    uint8_t mask_bit = prf_expanded_masks[bitpos][maskedDistances[actual_comparison_index]];
                    results[actual_comparison_index] ^= mask_bit;
                }
            }
        }
    }

    receiver_timer.setTimePoint("Receive and Unmask Messages");

    auto com = chl.bytesReceived() + chl.bytesSent();
    std::cout << "############ Receiver Summary ############" << std::endl;
    std::cout << receiver_timer << std::endl;
    std::cout << "Communication: " << com << " bytes (" << com / (1024.0 * 1024.0 * 1024.0) << " GB)" << std::endl;

    return results;
}