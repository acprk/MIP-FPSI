#include "distance_computation.h"
#include "../util/ot.h"
#include "../util/prf.h"
#include <cmath>

std::vector<uint16_t> runBatchedDistanceComputationResponder(const std::vector<BitVector> &responder_vectors, coproto::Socket &chl, const config &cfg, size_t &total_communication)
{
    assert(cfg.modulus > 0 && (size_t)cfg.modulus > cfg.vector_size);
    assert(pow(2, cfg.p_bits) >= cfg.modulus); // Ensure the modulus is less than or equal to 2^p_bits
    assert(responder_vectors.size() == cfg.responder_size);

    PRNG prng(osuCrypto::sysRandomSeed());

    Timer sender_timer;
    sender_timer.setTimePoint("start");
    size_t com = 0;

    // construct a vector to stored the random send messages.
    size_t num_ots = cfg.querier_size * cfg.vector_size;
    AlignedUnVector<std::array<block, 2>> sMsgs(num_ots);
    std::vector<uint16_t> mask_sums(cfg.querier_size * cfg.responder_size, 0);
    sender_timer.setTimePoint("Memory allocation");

    std::unique_ptr<osuCrypto::OtExtSender> sender = getOTExtSender(cfg, num_ots);

    // Now compute the base OTs, we need to set them on the first pair of extenders.
    // In real code you would only have a sender or reciever, not both. But we do
    // here just showing the example.
    osuCrypto::DefaultBaseOT base;
    BitVector bv(sender->baseOtCount());
    std::vector<block> baseMsg(sender->baseOtCount());
    bv.randomize(prng);

    // perform the base To, call sync_wait to block until they have completed.
    osuCrypto::cp::sync_wait(base.receive(bv, baseMsg, prng, chl));
    sender->setBaseOts(baseMsg, bv);
    sender_timer.setTimePoint("Base OTs");

    osuCrypto::cp::sync_wait(sender->send(sMsgs, prng, chl));
    sender_timer.setTimePoint("Random OTs");

    size_t vals_per_unit64 = 64 / cfg.p_bits;

    // Parameters for expanding one OT message
    size_t prf_vals_required = cfg.responder_size;
    size_t prf_uint64_required = (prf_vals_required + vals_per_unit64 - 1) / vals_per_unit64;
    size_t prf_output_bits = ceil_prf_size(cfg, prf_uint64_required * 64);
    size_t buffer_size_uint64 = (prf_output_bits + 63) / 64;

    uint64_t prf_msg_0[buffer_size_uint64];
    uint64_t prf_msg_1[buffer_size_uint64];

    if (cfg.querier_size == 1)
    {
        //////////////////////////////////////////////////////////
        //////////////         SINGLE QUERY         //////////////
        //////////////////////////////////////////////////////////

        for (size_t vector_pos = 0; vector_pos < cfg.vector_size; ++vector_pos)
        {
            size_t ot_index = vector_pos;

            prf(cfg, (uint8_t *)sMsgs[ot_index][0].data(), buffer_size_uint64 * 64, (uint8_t *)prf_msg_0);
            prf(cfg, (uint8_t *)sMsgs[ot_index][1].data(), buffer_size_uint64 * 64, (uint8_t *)prf_msg_1);

            std::vector<uint64_t> correlation((cfg.responder_size + vals_per_unit64 - 1) / vals_per_unit64, 0);

            for (size_t responder_index = 0; responder_index < cfg.responder_size; ++responder_index)
            {
                size_t uint64_index = responder_index / vals_per_unit64;
                size_t offset_in_uint64 = responder_index % vals_per_unit64;

                int64_t val_0 = (prf_msg_0[uint64_index] >> offset_in_uint64) & ((1ull << cfg.p_bits) - 1);
                val_0 = MOD(val_0, cfg.modulus);
                int64_t val_1 = (prf_msg_1[uint64_index] >> offset_in_uint64) & ((1ull << cfg.p_bits) - 1);
                val_1 = MOD(val_1, cfg.modulus);

                int64_t mask_plus_bit = val_0;
                int64_t bit = responder_vectors[responder_index][vector_pos];

                int64_t mask = mask_plus_bit - bit;
                if (mask < 0)
                    mask += cfg.modulus;

                uint64_t mask_plus_neg_bit = (mask + (bit ? 0 : 1)) % cfg.modulus;

                size_t result_index = responder_index;

                mask_sums[result_index] += mask;
                mask_sums[result_index] %= cfg.modulus;

                size_t correlation_index_uint64 = responder_index / vals_per_unit64;
                size_t correlation_offset_in_uint64 = responder_index % vals_per_unit64;

                // We mask the "mask_plus_neg_bit" with "val_1" and transfer the result in correlation
                uint64_t masked_mask_plus_neg_bit = (mask_plus_neg_bit + val_1) % cfg.modulus;

                correlation[correlation_index_uint64] |= (masked_mask_plus_neg_bit << (correlation_offset_in_uint64 * cfg.p_bits));
            }
            osuCrypto::cp::sync_wait(chl.send(correlation));
        }
    }
    else
    {
        //////////////////////////////////////////////////////////////
        //////////////         MULTIPLE QUERIES         //////////////
        //////////////////////////////////////////////////////////////

        for (size_t querier_index = 0; querier_index < cfg.querier_size; ++querier_index)
        {
            if (cfg.querier_size > 16 && querier_index % (cfg.querier_size / 16) == 0)
            {
                std::cout << "[Responder] Querier index: " << querier_index << std::endl;
            }

            std::vector<uint64_t> correlation((cfg.responder_size * cfg.vector_size + vals_per_unit64 - 1) / vals_per_unit64, 0);
            for (size_t vector_pos = 0; vector_pos < cfg.vector_size; ++vector_pos)
            {
                size_t ot_index = querier_index * cfg.vector_size + vector_pos;

                prf(cfg, (uint8_t *)sMsgs[ot_index][0].data(), buffer_size_uint64 * 64, (uint8_t *)prf_msg_0);
                prf(cfg, (uint8_t *)sMsgs[ot_index][1].data(), buffer_size_uint64 * 64, (uint8_t *)prf_msg_1);

                for (size_t responder_index = 0; responder_index < cfg.responder_size; ++responder_index)
                {
                    size_t uint64_index = responder_index / vals_per_unit64;
                    size_t offset_in_uint64 = responder_index % vals_per_unit64;

                    int64_t val_0 = (prf_msg_0[uint64_index] >> offset_in_uint64) & ((1ull << cfg.p_bits) - 1);
                    val_0 = MOD(val_0, cfg.modulus);
                    int64_t val_1 = (prf_msg_1[uint64_index] >> offset_in_uint64) & ((1ull << cfg.p_bits) - 1);
                    val_1 = MOD(val_1, cfg.modulus);

                    int64_t mask_plus_bit = val_0;
                    int64_t bit = responder_vectors[responder_index][vector_pos];

                    int64_t mask = mask_plus_bit - bit;
                    if (mask < 0)
                        mask += cfg.modulus;

                    uint64_t mask_plus_neg_bit = (mask + (bit ? 0 : 1)) % cfg.modulus;

                    size_t result_index = querier_index * cfg.responder_size + responder_index;

                    mask_sums[result_index] += mask;
                    mask_sums[result_index] %= cfg.modulus;

                    size_t correlation_index_uint64 = (vector_pos + (responder_index * cfg.vector_size)) / vals_per_unit64;
                    size_t correlation_offset_in_uint64 = (vector_pos + (responder_index * cfg.vector_size)) % vals_per_unit64;

                    // We mask the "mask_plus_neg_bit" with "val_1" and transfer the result in correlation
                    uint64_t masked_mask_plus_neg_bit = (mask_plus_neg_bit + val_1) % cfg.modulus;

                    correlation[correlation_index_uint64] |= (masked_mask_plus_neg_bit << (correlation_offset_in_uint64 * cfg.p_bits));
                }
            }
            osuCrypto::cp::sync_wait(chl.send(correlation));
        }
    }

    // make sure all messages have been sent.
    osuCrypto::cp::sync_wait(chl.flush());
    sender_timer.setTimePoint("Correlation transfer");

    com += chl.bytesReceived() + chl.bytesSent();

    std::cout << "############ Responder Distance Computation ############" << std::endl;
    std::cout << sender_timer << std::endl;
    std::cout << "Communication: " << com << " bytes (" << com / (1024.0 * 1024.0 * 1024.0) << " GB)" << std::endl;
    total_communication += com;

    return mask_sums;
}

void computeQuerierChoices(const std::vector<BitVector> &querier_vectors, const config &cfg, BitVector &querierChoices)
{
    assert(querier_vectors.size() == cfg.querier_size);
    assert(querierChoices.size() == cfg.querier_size * cfg.vector_size);

    for (size_t querier_index = 0; querier_index < querier_vectors.size(); ++querier_index)
    {
        for (size_t vector_pos = 0; vector_pos < cfg.vector_size; ++vector_pos)
        {
            querierChoices[querier_index * cfg.vector_size + vector_pos] = querier_vectors[querier_index][vector_pos];
        }
    }
}

std::vector<uint16_t> runBatchedDistanceComputationQuerier(const std::vector<BitVector> &querier_vectors, coproto::Socket &_chl, const config &cfg)
{
    assert(cfg.modulus > 0 && (size_t)cfg.modulus > cfg.vector_size);
    assert(pow(2, cfg.p_bits) >= cfg.modulus); // Ensure the modulus is less than or equal to 2^p_bits
    assert(querier_vectors.size() == cfg.querier_size);

    coproto::Socket &chl = _chl;
    PRNG prng(osuCrypto::sysRandomSeed());

    Timer receiver_timer;
    receiver_timer.setTimePoint("start");

    size_t num_ots = cfg.querier_size * cfg.vector_size;
    BitVector choices = BitVector(num_ots);
    computeQuerierChoices(querier_vectors, cfg, choices);
    receiver_timer.setTimePoint("Choice computation");

    std::unique_ptr<osuCrypto::OtExtReceiver> receiver = getOTExtReceiver(cfg, num_ots);

    // Now compute the base OTs, we need to set them on the first pair of extenders.
    // In real code you would only have a sender or reciever, not both. But we do
    // here just showing the example.
    osuCrypto::DefaultBaseOT base;
    std::vector<std::array<block, 2>> baseMsg(receiver->baseOtCount());

    // perform the base To, call sync_wait to block until they have completed.
    osuCrypto::cp::sync_wait(base.send(baseMsg, prng, chl));
    receiver->setBaseOts(baseMsg);
    receiver_timer.setTimePoint("Base OTs");

    // construct a vector to stored the received messages.
    AlignedUnVector<block> rMsgs(num_ots);

    osuCrypto::cp::sync_wait(receiver->receive(choices, rMsgs, prng, chl));
    receiver_timer.setTimePoint("Random OTs");

    size_t vals_per_unit64 = 64 / cfg.p_bits;

    // Parameters for expanding one OT message
    size_t prf_vals_required = cfg.responder_size;
    size_t prf_uint64_required = (prf_vals_required + vals_per_unit64 - 1) / vals_per_unit64;
    size_t prf_output_bits = ceil_prf_size(cfg, prf_uint64_required * 64);
    size_t buffer_size_uint64 = (prf_output_bits + 63) / 64;

    uint64_t prf_msg[buffer_size_uint64];
    std::vector<uint16_t> masked_distances(cfg.querier_size * cfg.responder_size, 0);

    if (cfg.querier_size == 1)
    {
        //////////////////////////////////////////////////////////
        //////////////         SINGLE QUERY         //////////////
        //////////////////////////////////////////////////////////

        for (size_t vector_pos = 0; vector_pos < cfg.vector_size; ++vector_pos)
        {
            std::vector<uint64_t> correlation((cfg.responder_size + vals_per_unit64 - 1) / vals_per_unit64, 0);
            osuCrypto::cp::sync_wait(chl.recv(correlation));

            size_t ot_index = vector_pos;

            prf(cfg, (uint8_t *)rMsgs[ot_index].data(), buffer_size_uint64 * 64, (uint8_t *)prf_msg);

            int64_t bit = querier_vectors[0][vector_pos];

            for (size_t responder_index = 0; responder_index < cfg.responder_size; ++responder_index)
            {
                size_t uint64_index = responder_index / vals_per_unit64;
                size_t offset_in_uint64 = responder_index % vals_per_unit64;

                int64_t val = (prf_msg[uint64_index] >> offset_in_uint64) & ((1ull << cfg.p_bits) - 1);
                val = MOD(val, cfg.modulus);

                if (bit)
                {
                    size_t correlation_index_uint64 = responder_index / vals_per_unit64;
                    size_t correlation_offset_in_uint64 = responder_index % vals_per_unit64;

                    assert(correlation_index_uint64 < correlation.size());
                    int64_t correlation_val = (correlation[correlation_index_uint64] >> (correlation_offset_in_uint64 * cfg.p_bits)) & ((1ull << cfg.p_bits) - 1);
                    correlation_val = correlation_val % cfg.modulus;

                    val = MOD(correlation_val - val, cfg.modulus);
                }

                size_t result_index = responder_index;
                masked_distances[result_index] += val;
                masked_distances[result_index] %= cfg.modulus;
            }
        }
    }
    else
    {
        //////////////////////////////////////////////////////////////
        //////////////         MULTIPLE QUERIES         //////////////
        //////////////////////////////////////////////////////////////

        for (size_t querier_index = 0; querier_index < cfg.querier_size; ++querier_index)
        {
            std::vector<uint64_t> correlation((cfg.responder_size * cfg.vector_size + vals_per_unit64 - 1) / vals_per_unit64, 0);
            osuCrypto::cp::sync_wait(chl.recv(correlation));

            for (size_t vector_pos = 0; vector_pos < cfg.vector_size; ++vector_pos)
            {

                size_t ot_index = querier_index * cfg.vector_size + vector_pos;

                prf(cfg, (uint8_t *)rMsgs[ot_index].data(), buffer_size_uint64 * 64, (uint8_t *)prf_msg);

                int64_t bit = querier_vectors[querier_index][vector_pos];

                for (size_t responder_index = 0; responder_index < cfg.responder_size; ++responder_index)
                {
                    size_t uint64_index = responder_index / vals_per_unit64;
                    size_t offset_in_uint64 = responder_index % vals_per_unit64;

                    int64_t val = (prf_msg[uint64_index] >> offset_in_uint64) & ((1ull << cfg.p_bits) - 1);
                    val = MOD(val, cfg.modulus);

                    if (bit)
                    {
                        size_t correlation_index_uint64 = (vector_pos + (responder_index * cfg.vector_size)) / vals_per_unit64;
                        size_t correlation_offset_in_uint64 = (vector_pos + (responder_index * cfg.vector_size)) % vals_per_unit64;

                        assert(correlation_index_uint64 < correlation.size());
                        int64_t correlation_val = (correlation[correlation_index_uint64] >> (correlation_offset_in_uint64 * cfg.p_bits)) & ((1ull << cfg.p_bits) - 1);
                        correlation_val = correlation_val % cfg.modulus;

                        val = MOD(correlation_val - val, cfg.modulus);
                    }

                    size_t result_index = querier_index * cfg.responder_size + responder_index;

                    masked_distances[result_index] += val;
                    masked_distances[result_index] %= cfg.modulus;
                }
            }
        }
    }
    receiver_timer.setTimePoint("Correlation transfer and extraction");

    auto com = chl.bytesReceived() + chl.bytesSent();
    std::cout << "############ Querier Distance Computation ############" << std::endl;
    std::cout << receiver_timer << std::endl;
    std::cout << "Communication: " << com << " bytes (" << com / (1024.0 * 1024.0 * 1024.0) << " GB)" << std::endl;

    return masked_distances;
}