#pragma once
#include "libOTe/TwoChooseOne/Iknp/IknpOtExtReceiver.h"
#include "libOTe/TwoChooseOne/Iknp/IknpOtExtSender.h"
#include "libOTe/TwoChooseOne/SoftSpokenOT/SoftSpokenShOtExt.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtSender.h"
#include "../config.h"
#include <memory>


inline std::unique_ptr<osuCrypto::OtExtSender> getOTExtSender(const config &cfg, size_t num_ots) {
    if(cfg.ot == VAL_OT_IKNP) {
        return std::make_unique<osuCrypto::IknpOtExtSender>();
    } else if(cfg.ot == VAL_OT_SOFTSPOKEN) {
        return std::make_unique<osuCrypto::SoftSpokenShOtSender<>>();
    } else if(cfg.ot == VAL_OT_SILENT) {
        auto sender = std::make_unique<osuCrypto::SilentOtExtSender>();
        sender->configure(num_ots, 2, 1, osuCrypto::SilentSecType::SemiHonest);
        return sender;
    }

    throw std::runtime_error("Unknown OT Protocol: " + cfg.ot);
}

inline std::unique_ptr<osuCrypto::OtExtReceiver> getOTExtReceiver(const config &cfg, size_t num_ots) {
    if(cfg.ot == VAL_OT_IKNP) {
        return std::make_unique<osuCrypto::IknpOtExtReceiver>();
    } else if(cfg.ot == VAL_OT_SOFTSPOKEN) {
        return std::make_unique<osuCrypto::SoftSpokenShOtReceiver<>>();
    } else if(cfg.ot == VAL_OT_SILENT) {
        auto receiver = std::make_unique<osuCrypto::SilentOtExtReceiver>();
        receiver->configure(num_ots, 2, 1, osuCrypto::SilentSecType::SemiHonest);
        return receiver;
    }

    throw std::runtime_error("Unknown OT Protocol: " + cfg.ot);
}