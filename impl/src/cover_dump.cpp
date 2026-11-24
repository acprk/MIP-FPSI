// Prints the public block cover used by net_bench (same public seed): one line per table with the
// bit positions of its key, in the order used to form the key. Used by the content-injection demo.
//   cover_dump -d 64 -delta 3 -b 4 -s 1
#include "fpsi.h"
#include "mip.h"

#include <cryptoTools/Common/CLP.h>
#include <iostream>

using namespace mip;

int main(int argc, char** argv)
{
    oc::CLP cmd(argc, argv);
    FpsiConfig cfg;
    auto p = MipParams::make(cmd.getOr("d", 64ull), cmd.getOr("delta", 3ull), cmd.getOr("b", 4ull),
                             cmd.getOr("s", 1ull), cfg.publicSeed);
    std::cout << "r " << p.r << "\n";
    for (auto& t : p.tables)
    {
        for (auto v : t) std::cout << v << " ";
        std::cout << "\n";
    }
    return 0;
}
