#include "distance_computation.h"
#include "../util/ot.h"
#include <cmath>

#define CORRELATION_BATCH_SIZE ((size_t)2048)
#define DEFAULT_OT_BATCH_SIZE ((size_t)512 * 512 * 512)
#define SILENT_OT_BATCH_SIZE (DEFAULT_OT_BATCH_SIZE / 2) // Requires more memory

size_t ot_batch_size(const config &cfg)
{
    if (cfg.ot == VAL_OT_SILENT)
        return SILENT_OT_BATCH_SIZE;
    else
        return DEFAULT_OT_BATCH_SIZE;
}

std::unique_ptr<osuCrypto::OtExtSender> get_ot_sender_with_base_ots(const config &cfg, size_t num_ots, Timer &sender_timer, PRNG &prng, coproto::AsioSocket &chl)
{
    std::unique_ptr<osuCrypto::OtExtSender> sender = getOTExtSender(cfg, num_ots);

    osuCrypto::DefaultBaseOT base;
    BitVector bv(sender->baseOtCount());
    std::vector<block> baseMsg(sender->baseOtCount());
    bv.randomize(prng);

    // perform the base To, call sync_wait to block until they have completed.
    osuCrypto::cp::sync_wait(base.receive(bv, baseMsg, prng, chl));
    sender->setBaseOts(baseMsg, bv);
    sender_timer.setTimePoint("Base OTs");

    return sender;
}

std::vector<uint16_t> runIndividualDistanceComputationResponder(const std::vector<BitVector> &responder_vectors, const std::optional<std::vector<BitVector>> &querier_shares, coproto::AsioSocket &chl, const config &cfg, size_t &total_communication)
{
    assert(cfg.modulus > 0 && (size_t)cfg.modulus > cfg.vector_size);
    assert(pow(2, cfg.p_bits) >= cfg.modulus); // Ensure the modulus is less than or equal to 2^p_bits
    assert(responder_vectors.size() == cfg.responder_size);

    PRNG prng(osuCrypto::sysRandomSeed());

    Timer sender_timer;
    sender_timer.setTimePoint("start");

    size_t ots_per_querier = cfg.vector_size * cfg.responder_size;
    size_t queriers_per_ot_batch = ot_batch_size(cfg) / ots_per_querier;
    if(queriers_per_ot_batch == 0)
    {
        std::cerr << "Warning: ot_batch_size is too small for the given configuration." << std::endl;
        queriers_per_ot_batch = 1; // Fallback to one querier per batch
    }

    size_t num_ot_batches = (cfg.querier_size + queriers_per_ot_batch - 1) / queriers_per_ot_batch;
    std::cout << "Number of OT batches: " << num_ot_batches << std::endl;

    std::unique_ptr<osuCrypto::OtExtSender> sender = nullptr;

    uint64_t random_ot_communication = 0;

    std::vector<uint16_t> mask_sums(cfg.responder_size * cfg.querier_size, 0);

    for (size_t ot_batch_index = 0; ot_batch_index < num_ot_batches; ++ot_batch_index)
    {
        size_t querier_start_index = ot_batch_index * queriers_per_ot_batch;
        size_t querier_end_index = std::min(querier_start_index + queriers_per_ot_batch, cfg.querier_size);

        size_t num_ots = ots_per_querier * (querier_end_index - querier_start_index);

        auto random_messages = AlignedUnVector<std::array<block, 2>>(num_ots);

        size_t comm_before_random_ot = chl.bytesReceived() + chl.bytesSent();

        if(sender == nullptr || cfg.ot == VAL_OT_SILENT)
        {
            // Generate new sender with base OTs for each batch if silent OT or if sender is not initialized
            sender = get_ot_sender_with_base_ots(cfg, num_ots, sender_timer, prng, chl);
        }

        osuCrypto::cp::sync_wait(sender->send(random_messages, prng, chl));
        sender_timer.setTimePoint("Random OT");
        random_ot_communication += (chl.bytesReceived() + chl.bytesSent()) - comm_before_random_ot;

        size_t vals_per_unit64 = 64 / cfg.p_bits;
        size_t correlation_batch_size = std::min(CORRELATION_BATCH_SIZE, cfg.responder_size);
        std::vector<uint64_t> correlation((cfg.vector_size * correlation_batch_size + vals_per_unit64 - 1) / vals_per_unit64, 0);

        for (size_t querier_index = querier_start_index; querier_index < querier_end_index; ++querier_index)
        {
            size_t correlation_batches = (cfg.responder_size + correlation_batch_size - 1) / correlation_batch_size;
            for (size_t batch_index = 0; batch_index < correlation_batches; ++batch_index)
            {
                size_t responder_start_index = batch_index * correlation_batch_size;
                size_t responder_end_index = std::min(responder_start_index + correlation_batch_size, cfg.responder_size);

                for (size_t responder_index = responder_start_index; responder_index < responder_end_index; ++responder_index)
                {
                    for (size_t vector_pos = 0; vector_pos < cfg.vector_size; ++vector_pos)
                    {

                        size_t ot_index = vector_pos + responder_index * cfg.vector_size + (querier_index - querier_start_index) * cfg.vector_size * cfg.responder_size;

                        int64_t val_0 = (*((uint64_t *)random_messages[ot_index][0].data()));
                        val_0 = MOD(val_0, cfg.modulus);
                        int64_t val_1 = (*((uint64_t *)random_messages[ot_index][1].data()));
                        val_1 = MOD(val_1, cfg.modulus);

                        int64_t mask_plus_bit = val_0 % cfg.modulus;
                        mask_plus_bit = MOD(mask_plus_bit, cfg.modulus);

                        size_t bit = responder_vectors[responder_index][vector_pos];
                        if (querier_shares.has_value())
                        {
                            bit ^= (*querier_shares)[querier_index][vector_pos];
                        }

                        int64_t mask = mask_plus_bit - bit;
                        if (mask < 0)
                            mask += cfg.modulus;

                        size_t result_index = querier_index * cfg.responder_size + responder_index;
                        mask_sums[result_index] += mask;
                        mask_sums[result_index] %= cfg.modulus;

                        uint64_t mask_plus_neg_bit = (mask + (bit ? 0 : 1)) % cfg.modulus;

                        size_t index_in_batch = (responder_index - responder_start_index) * cfg.vector_size + vector_pos;
                        size_t correlation_index_uint64 = index_in_batch / vals_per_unit64;
                        size_t correlation_offset_in_uint64 = index_in_batch % vals_per_unit64;

                        // We mask the "mask_plus_neg_bit" with "val_1" and transfer the result in correlation
                        uint64_t masked_mask_plus_neg_bit = (mask_plus_neg_bit + val_1) % cfg.modulus;

                        correlation[correlation_index_uint64] |= (masked_mask_plus_neg_bit << (correlation_offset_in_uint64 * cfg.p_bits));
                    }
                }
                osuCrypto::cp::sync_wait(chl.send(correlation));
                // Clear the correlation for the next batch
                std::fill(correlation.begin(), correlation.end(), 0);
            }
        }
        sender_timer.setTimePoint("Correlation computation and transfer");
    }

    // make sure all messages have been sent.
    osuCrypto::cp::sync_wait(chl.flush());
    sender_timer.setTimePoint("Flush");

    auto com = chl.bytesReceived() + chl.bytesSent();

    std::cout << "############ Sender Distance Computation ############" << std::endl;
    std::cout << sender_timer << std::endl;
    std::cout << "Communication: " << com << " bytes (" << com / (1024.0 * 1024.0 * 1024.0) << " GB)" << std::endl;
    std::cout << "Random OT Communication: " << random_ot_communication << " bytes ("
              << random_ot_communication / (1024.0 * 1024.0 * 1024.0) << " GB)" << std::endl;

    total_communication += com;

    return mask_sums;
}

void prepareReceiverChoices(const std::vector<BitVector> &vectors, const std::optional<std::vector<BitVector>> &responder_shares, const config &cfg, BitVector &receiverChoices, size_t querier_start_index, size_t querier_end_index)
{
    assert(receiverChoices.size() == 0);
    receiverChoices.reserve(vectors.size() * cfg.responder_size * cfg.vector_size);

    if (responder_shares.has_value())
    {
        const auto &shares = *responder_shares;
        for (size_t querier_index = querier_start_index; querier_index < querier_end_index; ++querier_index)
        {
            for (size_t responder_index = 0; responder_index < cfg.responder_size; ++responder_index)
            {
                auto temp = vectors[querier_index] ^ shares[responder_index];
                receiverChoices.append(temp);
            }
        }
    }
    else
    {
        for (size_t querier_index = querier_start_index; querier_index < querier_end_index; ++querier_index)
        {
            for (size_t responder_index = 0; responder_index < cfg.responder_size; ++responder_index)
            {
                receiverChoices.append(vectors[querier_index]);
            }
        }
    }
}

std::unique_ptr<osuCrypto::OtExtReceiver> get_ot_receiver_with_base_ots(const config &cfg, size_t num_ots, Timer &receiver_timer, PRNG &prng, coproto::AsioSocket &chl)
{
    std::unique_ptr<osuCrypto::OtExtReceiver> receiver = getOTExtReceiver(cfg, num_ots);

    osuCrypto::DefaultBaseOT base;
    std::vector<std::array<block, 2>> baseMsg(receiver->baseOtCount());

    osuCrypto::cp::sync_wait(base.send(baseMsg, prng, chl));
    receiver->setBaseOts(baseMsg);
    receiver_timer.setTimePoint("Base OTs");
    return receiver;
}

std::vector<uint16_t> runIndividualDistanceComputationQuerier(const std::vector<BitVector> &querier_vectors, const std::optional<std::vector<BitVector>> &responder_shares, coproto::AsioSocket &chl, const config &cfg)
{
    assert(cfg.modulus > 0 && (size_t)cfg.modulus > cfg.vector_size);
    assert(pow(2, cfg.p_bits) >= cfg.modulus); // Ensure the modulus is less than or equal to 2^p_bits
    assert(querier_vectors.size() == cfg.querier_size);

    PRNG prng(osuCrypto::sysRandomSeed());

    Timer receiver_timer;
    receiver_timer.setTimePoint("start");

    size_t ots_per_querier = cfg.vector_size * cfg.responder_size;
    size_t queriers_per_ot_batch = ot_batch_size(cfg) / ots_per_querier;
    if(queriers_per_ot_batch == 0)
    {
        std::cerr << "Warning: ot_batch_size is too small for the given configuration." << std::endl;
        queriers_per_ot_batch = 1; // Fallback to one querier per batch
    }
    size_t num_ot_batches = (cfg.querier_size + queriers_per_ot_batch - 1) / queriers_per_ot_batch;
    std::cout << "Number of OT batches: " << num_ot_batches << std::endl;

    std::unique_ptr<osuCrypto::OtExtReceiver> receiver = nullptr;

    std::vector<uint16_t> masked_distances(cfg.querier_size * cfg.responder_size, 0);

    for (size_t ot_batch_index = 0; ot_batch_index < num_ot_batches; ++ot_batch_index)
    {
        size_t querier_start_index = ot_batch_index * queriers_per_ot_batch;
        size_t querier_end_index = std::min(querier_start_index + queriers_per_ot_batch, cfg.querier_size);

        size_t num_ots = ots_per_querier * (querier_end_index - querier_start_index);

        if(receiver == nullptr || cfg.ot == VAL_OT_SILENT)
        {
            receiver = get_ot_receiver_with_base_ots(cfg, num_ots, receiver_timer, prng, chl);
        }
        
        BitVector choices;
        prepareReceiverChoices(querier_vectors, responder_shares, cfg, choices, querier_start_index, querier_end_index);
        receiver_timer.setTimePoint("Choice computation");

        // construct a vector to stored the received messages.
        AlignedUnVector<block> rMsgs(num_ots);

        osuCrypto::cp::sync_wait(receiver->receive(choices, rMsgs, prng, chl));
        // make sure all messages have been sent.
        osuCrypto::cp::sync_wait(chl.flush());
        receiver_timer.setTimePoint("Random OTs");

        size_t vals_per_unit64 = 64 / cfg.p_bits;
        size_t correlation_batch_size = std::min(CORRELATION_BATCH_SIZE, cfg.responder_size);

        std::vector<uint64_t> correlation((cfg.vector_size * correlation_batch_size + vals_per_unit64 - 1) / vals_per_unit64, 0);

        for (size_t querier_index = querier_start_index; querier_index < querier_end_index; ++querier_index)
        {
            size_t correlation_batches = (cfg.responder_size + correlation_batch_size - 1) / correlation_batch_size;
            for (size_t batch_index = 0; batch_index < correlation_batches; ++batch_index)
            {
                size_t responder_start_index = batch_index * correlation_batch_size;
                size_t responder_end_index = std::min(responder_start_index + correlation_batch_size, cfg.responder_size);

                osuCrypto::cp::sync_wait(chl.recv(correlation));

                for (size_t responder_index = responder_start_index; responder_index < responder_end_index; ++responder_index)
                {
                    for (size_t vector_pos = 0; vector_pos < cfg.vector_size; ++vector_pos)
                    {

                        size_t ot_index = vector_pos + responder_index * cfg.vector_size + (querier_index - querier_start_index) * cfg.vector_size * cfg.responder_size;

                        int64_t val = (*((uint64_t *)rMsgs[ot_index].data()));
                        val = MOD(val, cfg.modulus);
                        size_t bit = querier_vectors[querier_index][vector_pos];
                        if (responder_shares.has_value())
                        {
                            bit ^= (*responder_shares)[responder_index][vector_pos];
                        }

                        if (bit == 1)
                        {
                            size_t index_in_batch = (responder_index - responder_start_index) * cfg.vector_size + vector_pos;
                            size_t correlation_index_uint64 = index_in_batch / vals_per_unit64;
                            size_t correlation_offset_in_uint64 = index_in_batch % vals_per_unit64;

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
        receiver_timer.setTimePoint("Correlation transfer and Distance extraction");
    }


    auto com = chl.bytesReceived() + chl.bytesSent();
    std::cout << "############ Receiver Distance Computation ############" << std::endl;
    std::cout << receiver_timer << std::endl;
    std::cout << "Communication: " << com << " bytes (" << com / (1024.0 * 1024.0 * 1024.0) << " GB)" << std::endl;

    return masked_distances;
}
