#include "circuits.h"

#include <cryptoTools/Common/BitVector.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <optional>
#include <stdexcept>

namespace mip
{
    using oc::BetaWire;
    using oc::GateType;
    using volePSI::BetaBundle;
    using volePSI::BetaCircuit;

    namespace
    {
        struct Builder
        {
            BetaCircuit& c;

            BetaWire fresh()
            {
                BetaBundle t(1);
                c.addTempWireBundle(t);
                return t[0];
            }
            BetaWire gate(BetaWire a, BetaWire b, GateType g)
            {
                auto o = fresh();
                c.addGate(a, b, g, o);
                return o;
            }
            BetaWire inv(BetaWire a)
            {
                auto o = fresh();
                c.addInvert(a, o);
                return o;
            }

        };

        // symbolic bit: constant or wire
        struct Sym
        {
            bool isConst;
            bool val;
            BetaWire w;
        };
    }

    BetaCircuit thresholdCircuit(u64 d, u64 delta, u64 tau)
    {
        BetaCircuit c;
        BetaBundle in(d + tau), out(1);
        c.addInputBundle(in);
        c.addOutputBundle(out);
        Builder b{c};
        BetaBundle v;
        v.mWires.assign(in.mWires.begin(), in.mWires.begin() + d);

        // Degenerate case (e.g. the full-key cover / ball expansion): every counted vector has
        // weight <= d <= delta, so only the tag decides: out = NOR(tag bits).
        if (delta >= d)
        {
            if (tau == 0)
                throw std::runtime_error("degenerate threshold without tag");
            std::vector<BetaWire> rej(in.mWires.begin() + d, in.mWires.end());
            if (rej.size() == 1)
            {
                auto cp = b.fresh();
                c.addCopy(rej[0], cp);
                rej.push_back(cp);
            }
            while (rej.size() > 2)
            {
                std::vector<BetaWire> nxt;
                for (u64 i = 0; i + 1 < rej.size(); i += 2)
                    nxt.push_back(b.gate(rej[i], rej[i + 1], GateType::Or));
                if (rej.size() % 2)
                    nxt.push_back(rej.back());
                rej = nxt;
            }
            c.addGate(rej[0], rej[1], GateType::Nor, out[0]);
            c.levelByAndDepth();
            return c;
        }

        // Carry-save (Wallace) popcount truncated at weight W = bitlen(delta): columns 0..W-1 are
        // compressed to one bit each with full/half adders (1 AND each); every bit that reaches
        // column W means count > delta, so those are only OR-ed together.
        u64 W = 0;
        while ((1ull << W) <= delta)
            ++W;
        std::vector<std::vector<BetaWire>> col(W + 1);
        col[0] = v.mWires;
        for (u64 w = 0; w < W; ++w)
        {
            auto& cw = col[w];
            u64 head = 0;
            while (cw.size() - head >= 3)
            {
                auto a = cw[head], x = cw[head + 1], c = cw[head + 2];
                head += 3;
                auto t1 = b.gate(a, c, GateType::Xor);
                auto t2 = b.gate(x, c, GateType::Xor);
                cw.push_back(b.gate(t1, x, GateType::Xor));
                auto t3 = b.gate(t1, t2, GateType::And);
                col[w + 1].push_back(b.gate(t3, c, GateType::Xor));
            }
            if (cw.size() - head == 2)
            {
                auto a = cw[head], x = cw[head + 1];
                head += 2;
                cw.push_back(b.gate(a, x, GateType::Xor));
                col[w + 1].push_back(b.gate(a, x, GateType::And));
            }
            cw.erase(cw.begin(), cw.begin() + head);
        }
        std::vector<BetaWire> cnt;
        for (u64 w = 0; w < W; ++w)
            cnt.push_back(col[w][0]);
        // OR the overflow bits with a balanced tree (same AND count as a chain, log depth).
        std::optional<BetaWire> overflow;
        {
            std::vector<BetaWire> layer(col[W].begin(), col[W].end());
            while (layer.size() > 1)
            {
                std::vector<BetaWire> nxt;
                for (u64 i = 0; i + 1 < layer.size(); i += 2)
                    nxt.push_back(b.gate(layer[i], layer[i + 1], GateType::Or));
                if (layer.size() % 2)
                    nxt.push_back(layer.back());
                layer = nxt;
            }
            if (!layer.empty())
                overflow = layer[0];
        }
        u64 width = W;

        // gt = [cnt > delta], MSB to LSB with constant folding.
        auto AND = [&](Sym x, Sym y) -> Sym {
            if (x.isConst) return x.val ? y : Sym{true, false, 0};
            if (y.isConst) return y.val ? x : Sym{true, false, 0};
            return {false, false, b.gate(x.w, y.w, GateType::And)};
        };
        auto OR = [&](Sym x, Sym y) -> Sym {
            if (x.isConst) return x.val ? Sym{true, true, 0} : y;
            if (y.isConst) return y.val ? Sym{true, true, 0} : x;
            return {false, false, b.gate(x.w, y.w, GateType::Or)};
        };
        auto NOT = [&](Sym x) -> Sym {
            if (x.isConst) return {true, !x.val, 0};
            return {false, false, b.inv(x.w)};
        };

        if (delta >= d)
            throw std::runtime_error("delta >= d");
        Sym gt{true, false, 0}, eq{true, true, 0};
        for (u64 k = width; k-- > 0;)
        {
            Sym ci = k < cnt.size() ? Sym{false, false, cnt[k]} : Sym{true, false, 0};
            if (((delta >> k) & 1) == 0)
            {
                gt = OR(gt, AND(eq, ci));
                eq = AND(eq, NOT(ci));
            }
            else
                eq = AND(eq, ci);
        }
        // reject terms: comparator, overflow, non-zero tag bits (garbage payload)
        std::vector<BetaWire> rej;
        if (gt.isConst && gt.val)
            throw std::runtime_error("degenerate threshold");
        if (!gt.isConst)
            rej.push_back(gt.w);
        if (overflow)
            rej.push_back(*overflow);
        for (u64 k = d; k < d + tau; ++k)
            rej.push_back(in[k]);
        if (rej.empty())
            throw std::runtime_error("degenerate threshold");
        // out = NOR(rej). cryptoTools inverts lazily via wire flags which GMW ignores on output
        // wires, so the final negation must be a real Nor gate.
        if (rej.size() == 1)
        {
            auto cp = b.fresh();
            c.addCopy(rej[0], cp);
            rej.push_back(cp);
        }
        while (rej.size() > 2)
        {
            std::vector<BetaWire> nxt;
            for (u64 i = 0; i + 1 < rej.size(); i += 2)
                nxt.push_back(b.gate(rej[i], rej[i + 1], GateType::Or));
            if (rej.size() % 2)
                nxt.push_back(rej.back());
            rej = nxt;
        }
        c.addGate(rej[0], rej[1], GateType::Nor, out[0]);
        c.levelByAndDepth();
        return c;
    }

    BetaCircuit orCircuit(u64 q)
    {
        BetaCircuit c;
        BetaBundle in(q), out(1);
        c.addInputBundle(in);
        c.addOutputBundle(out);
        Builder b{c};
        std::vector<BetaWire> layer = in.mWires;
        if (layer.size() == 1)
            c.addCopy(layer[0], out[0]);
        while (layer.size() > 1)
        {
            std::vector<BetaWire> nxt;
            for (u64 i = 0; i + 1 < layer.size(); i += 2)
            {
                if (layer.size() == 2)
                {
                    c.addGate(layer[0], layer[1], GateType::Or, out[0]);
                    nxt.push_back(out[0]);
                }
                else
                    nxt.push_back(b.gate(layer[i], layer[i + 1], GateType::Or));
            }
            if (layer.size() % 2)
                nxt.push_back(layer.back());
            layer = nxt;
        }
        c.levelByAndDepth();
        return c;
    }

    void circuitSelfTest()
    {
        oc::PRNG prng(oc::block(42, 7));
        for (u64 d : {16ull, 64ull, 128ull, 256ull})
            for (u64 delta : {1ull, 3ull, 4ull, 7ull, 15ull})
                for (u64 tau : {0ull, 5ull})
            {
                if (delta >= d)
                    continue;
                auto cir = thresholdCircuit(d, delta, tau);
                for (u64 t = 0; t < 300; ++t)
                {
                    oc::BitVector v(d + tau), o(1);
                    u64 w = (t % 2) ? prng.get<u64>() % (2 * delta + 3) : prng.get<u64>() % (d + 1);
                    if (w > d) w = d;
                    for (u64 k = 0; k < w; ++k)
                        v[prng.get<u64>() % d] = 1;
                    bool tagOk = true;
                    if (tau && (prng.get<std::uint8_t>() & 1))
                    {
                        v[d + prng.get<u64>() % tau] = 1;
                        tagOk = false;
                    }
                    u64 hw = 0;
                    for (u64 k = 0; k < d; ++k) hw += v[k];
                    std::vector<oc::BitVector> ins{v}, outs{o};
                    cir.evaluate(ins, outs, false);
                    bool exp = tagOk && hw <= delta;
                    if (outs[0][0] != exp)
                        throw std::runtime_error("threshold circuit mismatch d=" + std::to_string(d) +
                                                 " delta=" + std::to_string(delta));
                }
            }
        for (u64 q : {1ull, 2ull, 3ull, 7ull, 64ull, 1001ull})
        {
            auto cir = orCircuit(q);
            for (u64 t = 0; t < 100; ++t)
            {
                oc::BitVector v(q), o(1);
                if (t % 3)
                    v[prng.get<u64>() % q] = 1;
                std::vector<oc::BitVector> ins{v}, outs{o};
                cir.evaluate(ins, outs, false);
                if (outs[0][0] != (v.hammingWeight() > 0))
                    throw std::runtime_error("or circuit mismatch q=" + std::to_string(q));
            }
        }
    }
}
