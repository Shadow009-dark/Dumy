// chunker.h - splits long texts into overlapping token-sized chunks.
#pragma once

#include <string>
#include <vector>

namespace dumy {

// Normalizes whitespace, removes control characters and de-hyphenates words
// that PDF extraction split across lines. Must be called before chunking.
std::string normalize_text(const std::string& text);

// Splits `text` into chunks of about `size_tokens` tokens (estimated from
// words) with `overlap_tokens` of overlap between consecutive chunks so that
// no sentence is lost at a chunk boundary.
std::vector<std::string> chunk_text(const std::string& text,
                                    size_t size_tokens,
                                    size_t overlap_tokens);

}  // namespace dumy
