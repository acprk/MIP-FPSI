#include "distance_computation.h"
#include "../util/ot.h"
#include "../util/prf.h"
#include <cmath>
#include <bitset>

#define CORRELATION_BATCH_SIZE ((size_t)1024 * 1024)

std::vector<uint16_t> runSharedBatchedDistanceComputationResponder(const std::vector<BitVector> &responder_vectors, const std::optional<std::vector<BitVector>> &querier_shares, coproto::Socket &chl, const config &cfg, size_t &total_communication)
{
    assert(cfg.modulus > 0 && (size_t)cfg.modulus > cfg.vector_size);
    assert(pow(2, cfg.p_bits) >= cfg.modulus); // Ensure the modulus is less than or equal to 2^p_bits
    assert(responder_vectors.size() == cfg.responder_size);
    assert(cfg.secret_shared && querier_shares.has_value() && querier_shares->size() == cfg.querier_size);

    PRNG prng(osuCrypto::sysRandomSeed());

    Timer sender_timer;
    sender_timer.setTimePoint("start");

    size_t num_ots = cfg.vector_size * (cfg.responder_size + cfg.querier_size);
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

    auto random_messages = AlignedUnVector<std::array<block, 2>>(num_ots);

    osuCrypto::cp::sync_wait(sender->send(random_messages, prng, chl));
    sender_timer.setTimePoint("Random OT");
    auto random_ot_communication = chl.bytesReceived() + chl.bytesSent();

    size_t vals_per_unit64 = 64 / cfg.p_bits;
    std::vector<uint16_t> mask_sums(cfg.responder_size * cfg.querier_size, 0);

    size_t querier_share_masks_vals = cfg.responder_size;
    size_t querier_share_masks_uint64 = (4 * querier_share_masks_vals + vals_per_unit64 - 1) / vals_per_unit64;
    assert(ceil_prf_size(cfg, querier_share_masks_uint64 * 64) % 64 == 0); // Ensure PRF output size is a multiple of 64 bits
    querier_share_masks_uint64 = ceil_prf_size(cfg, querier_share_masks_uint64 * 64) / 64;
    uint64_t querier_share_mask_0[querier_share_masks_uint64];
    uint64_t querier_share_mask_1[querier_share_masks_uint64];

    size_t responder_share_masks_vals = cfg.querier_size;
    size_t responder_share_masks_uint64 = (4 * responder_share_masks_vals + vals_per_unit64 - 1) / vals_per_unit64;
    assert(ceil_prf_size(cfg, responder_share_masks_uint64 * 64) % 64 == 0); // Ensure PRF output size is a multiple of 64 bits
    responder_share_masks_uint64 = ceil_prf_size(cfg, responder_share_masks_uint64 * 64) / 64;
    std::unique_ptr<uint64_t[]> responder_share_masks_0(new uint64_t[cfg.responder_size * responder_share_masks_uint64]);
    std::unique_ptr<uint64_t[]> responder_share_masks_1(new uint64_t[cfg.responder_size * responder_share_masks_uint64]);

    size_t queriers_per_correlation_batch = CORRELATION_BATCH_SIZE / cfg.responder_size;
    queriers_per_correlation_batch = std::min(queriers_per_correlation_batch, cfg.querier_size);
    size_t correlation_batches = (cfg.querier_size + queriers_per_correlation_batch - 1) / queriers_per_correlation_batch;

    for (size_t vector_pos = 0; vector_pos < cfg.vector_size; ++vector_pos)
    {
        if ((vector_pos % (cfg.vector_size / 10)) == 0)
        {
            std::cout << "Processing vector position: " << vector_pos << std::endl;
        }

        for (size_t responder_index = 0; responder_index < cfg.responder_size; ++responder_index)
        {
            size_t ot_index = vector_pos * (cfg.responder_size + cfg.querier_size) + cfg.querier_size + responder_index;

            prf(cfg, random_messages[ot_index][0].data(), responder_share_masks_uint64 * 64, (uint8_t *)(responder_share_masks_0.get() + responder_index * responder_share_masks_uint64));
            prf(cfg, random_messages[ot_index][1].data(), responder_share_masks_uint64 * 64, (uint8_t *)(responder_share_masks_1.get() + responder_index * responder_share_masks_uint64));
        }

        for (size_t correlation_batch_index = 0; correlation_batch_index < correlation_batches; ++correlation_batch_index)
        {
            size_t querier_start_index = correlation_batch_index * queriers_per_correlation_batch;
            size_t querier_end_index = std::min(querier_start_index + queriers_per_correlation_batch, cfg.querier_size);
            size_t current_correlation_vals = (querier_end_index - querier_start_index) * cfg.responder_size * 3;

            std::vector<uint64_t> correlation((current_correlation_vals + vals_per_unit64 - 1) / vals_per_unit64, 0);

            for (size_t querier_index = querier_start_index; querier_index < querier_end_index; ++querier_index)
            {
                size_t ot_index = vector_pos * (cfg.responder_size + cfg.querier_size) + querier_index;
                prf(cfg, random_messages[ot_index][0].data(), querier_share_masks_uint64 * 64, (uint8_t *)querier_share_mask_0);
                prf(cfg, random_messages[ot_index][1].data(), querier_share_masks_uint64 * 64, (uint8_t *)querier_share_mask_1);

                for (size_t responder_index = 0; responder_index < cfg.responder_size; ++responder_index)
                {
                    size_t four_responder_index = responder_index * 4;
                    size_t four_querier_index = querier_index * 4;

                    size_t x00 = (querier_share_mask_0[four_responder_index / vals_per_unit64] >> (four_responder_index % vals_per_unit64 * cfg.p_bits)) % cfg.modulus;
                    size_t y00 = (responder_share_masks_0.get()[responder_index * responder_share_masks_uint64 + (four_querier_index / vals_per_unit64)] >> ((four_querier_index % vals_per_unit64) * cfg.p_bits)) % cfg.modulus;
                    int64_t m00 = (x00 + y00);
                    m00 %= cfg.modulus;

                    size_t x01 = (querier_share_mask_0[(four_responder_index + 1) / vals_per_unit64] >> ((four_responder_index + 1) % vals_per_unit64 * cfg.p_bits)) % cfg.modulus;
                    size_t y01 = (responder_share_masks_1.get()[responder_index * responder_share_masks_uint64 + (four_querier_index + 1) / vals_per_unit64] >> ((four_querier_index + 1) % vals_per_unit64 * cfg.p_bits)) % cfg.modulus;
                    int64_t m01 = (x01 + y01) % cfg.modulus;

                    size_t x10 = (querier_share_mask_1[(four_responder_index + 2) / vals_per_unit64] >> ((four_responder_index + 2) % vals_per_unit64 * cfg.p_bits)) % cfg.modulus;
                    size_t y10 = (responder_share_masks_0.get()[responder_index * responder_share_masks_uint64 + (four_querier_index + 2) / vals_per_unit64] >> ((four_querier_index + 2) % vals_per_unit64 * cfg.p_bits)) % cfg.modulus;
                    int64_t m10 = (x10 + y10) % cfg.modulus;

                    size_t x11 = (querier_share_mask_1[(four_responder_index + 3) / vals_per_unit64] >> ((four_responder_index + 3) % vals_per_unit64 * cfg.p_bits)) % cfg.modulus;
                    size_t y11 = (responder_share_masks_1.get()[responder_index * responder_share_masks_uint64 + (four_querier_index + 3) / vals_per_unit64] >> ((four_querier_index + 3) % vals_per_unit64 * cfg.p_bits)) % cfg.modulus;
                    int64_t m11 = (x11 + y11) % cfg.modulus;

                    size_t bit = responder_vectors[responder_index][vector_pos] ^ (*querier_shares)[querier_index][vector_pos];
                    int64_t mask = (m00 + (cfg.modulus - bit)) % cfg.modulus;
                    mask_sums[querier_index * cfg.responder_size + responder_index] += mask;
                    mask_sums[querier_index * cfg.responder_size + responder_index] %= cfg.modulus;

                    int64_t m1 = (mask + (bit ? 0 : 1)) % cfg.modulus;

                    int64_t mu01 = ((m1 + cfg.modulus) - m01) % cfg.modulus;
                    int64_t mu10 = ((m1 + cfg.modulus) - m10) % cfg.modulus;
                    int64_t mu11 = ((m00 + cfg.modulus) - m11) % cfg.modulus;

                    size_t correlation_index_01 = (querier_index - querier_start_index) * cfg.responder_size * 3 + responder_index * 3;
                    size_t correlation_index_10 = correlation_index_01 + 1;
                    size_t correlation_index_11 = correlation_index_01 + 2;

                    assert(m00 >= 0 && m00 < cfg.modulus);
                    assert(m1 >= 0 && m1 < cfg.modulus);

                    assert(correlation_index_11 / vals_per_unit64 < correlation.size());
                    correlation[correlation_index_01 / vals_per_unit64] |= (mu01 << (correlation_index_01 % vals_per_unit64 * cfg.p_bits));
                    correlation[correlation_index_10 / vals_per_unit64] |= (mu10 << (correlation_index_10 % vals_per_unit64 * cfg.p_bits));
                    correlation[correlation_index_11 / vals_per_unit64] |= (mu11 << (correlation_index_11 % vals_per_unit64 * cfg.p_bits));
                }
            }
            osuCrypto::cp::sync_wait(chl.send(correlation));
        }
    }

    // make sure all messages have been sent.
    osuCrypto::cp::sync_wait(chl.flush());
    sender_timer.setTimePoint("Correlation computation and transfer");

    auto com = chl.bytesReceived() + chl.bytesSent();

    std::cout << "############ Sender Distance Computation ############" << std::endl;
    std::cout << sender_timer << std::endl;
    std::cout << "Communication: " << com << " bytes (" << com / (1024.0 * 1024.0 * 1024.0) << " GB)" << std::endl;
    std::cout << "Random OT Communication: " << random_ot_communication << " bytes ("
              << random_ot_communication / (1024.0 * 1024.0 * 1024.0) << " GB)" << std::endl;
    total_communication += com;

    return mask_sums;
}

void prepareReceiverChoicesSecretSharedCorrelated(const std::vector<BitVector> &vectors, const std::optional<std::vector<BitVector>> &responder_shares, const config &cfg, BitVector &receiverChoices)
{
    assert(receiverChoices.size() == cfg.vector_size * (cfg.responder_size + cfg.querier_size));
    assert(responder_shares.has_value());
    assert(responder_shares->size() == cfg.responder_size);
    assert(vectors.size() == cfg.querier_size);

    for (size_t vector_pos = 0; vector_pos < cfg.vector_size; ++vector_pos)
    {
        for (size_t querier_index = 0; querier_index < cfg.querier_size; ++querier_index)
        {
            size_t ot_index = vector_pos * (cfg.responder_size + cfg.querier_size) + querier_index;
            receiverChoices[ot_index] = vectors[querier_index][vector_pos];
        }

        for (size_t responder_index = 0; responder_index < cfg.responder_size; ++responder_index)
        {
            size_t ot_index = vector_pos * (cfg.responder_size + cfg.querier_size) + cfg.querier_size + responder_index;
            receiverChoices[ot_index] = responder_shares->at(responder_index)[vector_pos];
        }
    }
}

std::vector<uint16_t> runSharedBatchedDistanceComputationQuerier(const std::vector<BitVector> &querier_vectors, const std::optional<std::vector<BitVector>> &responder_shares, coproto::Socket &chl, const config &cfg)
{
    assert(cfg.modulus > 0 && (size_t)cfg.modulus > cfg.vector_size);
    assert(pow(2, cfg.p_bits) >= cfg.modulus); // Ensure the modulus is less than or equal to 2^p_bits
    assert(querier_vectors.size() == cfg.querier_size);
    assert(cfg.secret_shared && responder_shares.has_value() && responder_shares->size() == cfg.responder_size);

    PRNG prng(osuCrypto::sysRandomSeed());

    Timer receiver_timer;
    receiver_timer.setTimePoint("start");

    size_t num_ots = cfg.vector_size * (cfg.responder_size + cfg.querier_size);
    BitVector choices = BitVector(num_ots);

    prepareReceiverChoicesSecretSharedCorrelated(querier_vectors, responder_shares, cfg, choices);
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
    // make sure all messages have been sent.
    osuCrypto::cp::sync_wait(chl.flush());
    receiver_timer.setTimePoint("Random OTs");

    size_t vals_per_unit64 = 64 / cfg.p_bits;
    std::vector<uint16_t> masked_distances(cfg.querier_size * cfg.responder_size, 0);

    size_t querier_share_masks_vals = cfg.responder_size;
    size_t querier_share_masks_uint64 = (4 * querier_share_masks_vals + vals_per_unit64 - 1) / vals_per_unit64;
    assert(ceil_prf_size(cfg, querier_share_masks_uint64 * 64) % 64 == 0); // Ensure PRF output size is a multiple of 64 bits
    querier_share_masks_uint64 = ceil_prf_size(cfg, querier_share_masks_uint64 * 64) / 64;
    uint64_t querier_share_mask[querier_share_masks_uint64];

    size_t responder_share_masks_vals = cfg.querier_size;
    size_t responder_share_masks_uint64 = (4 * responder_share_masks_vals + vals_per_unit64 - 1) / vals_per_unit64;
    assert(ceil_prf_size(cfg, responder_share_masks_uint64 * 64) % 64 == 0); // Ensure PRF output size is a multiple of 64 bits
    responder_share_masks_uint64 = ceil_prf_size(cfg, responder_share_masks_uint64 * 64) / 64;
    std::unique_ptr<uint64_t[]> responder_share_masks(new uint64_t[cfg.responder_size * responder_share_masks_uint64]);

    size_t queriers_per_correlation_batch = CORRELATION_BATCH_SIZE / cfg.responder_size;
    queriers_per_correlation_batch = std::min(queriers_per_correlation_batch, cfg.querier_size);
    size_t correlation_batches = (cfg.querier_size + queriers_per_correlation_batch - 1) / queriers_per_correlation_batch;

    for (size_t vector_pos = 0; vector_pos < cfg.vector_size; ++vector_pos)
    {
        // Expand the responder share masks
        size_t ot_index_base = vector_pos * (cfg.responder_size + cfg.querier_size) + cfg.querier_size;
        for (size_t responder_index = 0; responder_index < cfg.responder_size; ++responder_index)
        {
            prf(cfg, rMsgs[ot_index_base + responder_index].data(), responder_share_masks_uint64 * 64, (uint8_t *)(responder_share_masks.get() + responder_index * responder_share_masks_uint64));
        }

        for (size_t correlation_batch_index = 0; correlation_batch_index < correlation_batches; ++correlation_batch_index)
        {
            size_t querier_start_index = correlation_batch_index * queriers_per_correlation_batch;
            size_t querier_end_index = std::min(querier_start_index + queriers_per_correlation_batch, cfg.querier_size);
            size_t current_correlation_vals = (querier_end_index - querier_start_index) * cfg.responder_size * 3;

            std::vector<uint64_t> correlation((current_correlation_vals + vals_per_unit64 - 1) / vals_per_unit64, 0);

            osuCrypto::cp::sync_wait(chl.recv(correlation));

            for (size_t querier_index = querier_start_index; querier_index < querier_end_index; ++querier_index)
            {

                size_t ot_index = vector_pos * (cfg.responder_size + cfg.querier_size) + querier_index;
                prf(cfg, rMsgs[ot_index].data(), querier_share_masks_uint64 * 64, (uint8_t *)querier_share_mask);

                for (size_t responder_index = 0; responder_index < cfg.responder_size; ++responder_index)
                {
                    size_t o = 2 * querier_vectors[querier_index][vector_pos] + responder_shares->at(responder_index)[vector_pos];

                    size_t four_responder_index = responder_index * 4;
                    size_t four_querier_index = querier_index * 4;

                    size_t x = (querier_share_mask[(four_responder_index + o) / vals_per_unit64] >> ((four_responder_index + o) % vals_per_unit64 * cfg.p_bits)) % cfg.modulus;
                    size_t y = (responder_share_masks[responder_index * responder_share_masks_uint64 + (four_querier_index + o) / vals_per_unit64] >> ((four_querier_index + o) % vals_per_unit64 * cfg.p_bits)) % cfg.modulus;
                    int64_t d = (x + y) % cfg.modulus;

                    if (o > 0)
                    {
                        size_t correlation_index = (querier_index - querier_start_index) * cfg.responder_size * 3 + responder_index * 3 + (o - 1);
                        d += (((correlation[correlation_index / vals_per_unit64] >> (correlation_index % vals_per_unit64 * cfg.p_bits)) & ((1ull << cfg.p_bits) - 1))) % cfg.modulus;
                    }

                    masked_distances[querier_index * cfg.responder_size + responder_index] += d;
                    masked_distances[querier_index * cfg.responder_size + responder_index] %= cfg.modulus;
                }
            }
        }
    }

    receiver_timer.setTimePoint("Correlation transfer and Distance extraction");

    auto com = chl.bytesReceived() + chl.bytesSent();
    std::cout << "############ Receiver Distance Computation ############" << std::endl;
    std::cout << receiver_timer << std::endl;
    std::cout << "Communication: " << com << " bytes (" << com / (1024.0 * 1024.0 * 1024.0) << " GB)" << std::endl;

    return masked_distances;
}