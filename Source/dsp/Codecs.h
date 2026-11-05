#pragma once

// Native sample formats. Each codec encodes the 16-bit sample pool the way the
// console's tools would, then decodes it with the hardware's own decoder, so the
// result carries the format's quantisation noise and filter artefacts.

#include "Consoles.h"

#include <atomic>
#include <vector>

namespace lotus
{

/** Encodes then decodes `pool` with `codec`. Returns an empty vector if cancelled. */
std::vector<int16_t> applyCodec (const std::vector<int16_t>& pool, Codec codec,
                                 const std::atomic<bool>* cancel = nullptr);

} // namespace lotus
