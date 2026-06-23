#include "Utf8.h"

#include "Utf8ComposeTable.h"

namespace {
// Look up the canonical composition of (base + combining mark), or 0 if none.
uint32_t utf8ComposePair(const uint32_t base, const uint32_t mark) {
  if (base > 0xFFFF || mark > 0xFFFF) return 0;
  int lo = 0;
  int hi = kUtf8ComposeTableSize - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const Utf8ComposeEntry& e = kUtf8ComposeTable[mid];
    if (e.base < base || (e.base == base && e.mark < mark)) {
      lo = mid + 1;
    } else if (e.base > base || (e.base == base && e.mark > mark)) {
      hi = mid - 1;
    } else {
      return e.composed;
    }
  }
  return 0;
}
}  // namespace

std::string utf8ComposeNfc(const std::string& in) {
  // Fast path: NFC composition can only change text that contains a combining
  // diacritical mark U+0300-036F (UTF-8 lead byte 0xCC or 0xCD). Plain ASCII and
  // already-precomposed (NFC) text -- the vast majority of words -- have none, so
  // return them untouched without walking codepoints or allocating. A 0xCD that is
  // actually a non-combining codepoint just falls through to the full pass below.
  bool maybeHasMarks = false;
  for (const unsigned char c : in) {
    if (c == 0xCC || c == 0xCD) {
      maybeHasMarks = true;
      break;
    }
  }
  if (!maybeHasMarks) return in;

  std::string out;
  out.reserve(in.size());
  const unsigned char* p = reinterpret_cast<const unsigned char*>(in.c_str());
  uint32_t base = 0;
  bool haveBase = false;
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;
    if (utf8IsCombiningMark(cp)) {
      const uint32_t composed = haveBase ? utf8ComposePair(base, cp) : 0;
      if (composed) {
        base = composed;  // keep accumulating further marks onto the composed char
        continue;
      }
      // No composition: flush the pending base, then emit the mark unchanged.
      if (haveBase) {
        utf8AppendCodepoint(base, out);
        haveBase = false;
      }
      utf8AppendCodepoint(cp, out);
    } else {
      if (haveBase) utf8AppendCodepoint(base, out);
      base = cp;
      haveBase = true;
    }
  }
  if (haveBase) utf8AppendCodepoint(base, out);
  return out;
}

int utf8CodepointLen(const unsigned char c) {
  if (c < 0x80) return 1;          // 0xxxxxxx
  if ((c >> 5) == 0x6) return 2;   // 110xxxxx
  if ((c >> 4) == 0xE) return 3;   // 1110xxxx
  if ((c >> 3) == 0x1E) return 4;  // 11110xxx
  return 1;                        // fallback for invalid
}

uint32_t utf8NextCodepoint(const unsigned char** string) {
  if (**string == 0) {
    return 0;
  }

  const unsigned char lead = **string;
  const int bytes = utf8CodepointLen(lead);
  const uint8_t* chr = *string;

  // Invalid lead byte (stray continuation byte 0x80-0xBF, or 0xFE/0xFF)
  if (bytes == 1 && lead >= 0x80) {
    (*string)++;
    return REPLACEMENT_GLYPH;
  }

  if (bytes == 1) {
    (*string)++;
    return chr[0];
  }

  // Validate continuation bytes before consuming them
  for (int i = 1; i < bytes; i++) {
    if ((chr[i] & 0xC0) != 0x80) {
      // Missing or invalid continuation byte — skip all bytes consumed so far
      *string += i;
      return REPLACEMENT_GLYPH;
    }
  }

  uint32_t cp = chr[0] & ((1 << (7 - bytes)) - 1);  // mask header bits

  for (int i = 1; i < bytes; i++) {
    cp = (cp << 6) | (chr[i] & 0x3F);
  }

  // Reject overlong encodings, surrogates, and out-of-range values
  const bool overlong = (bytes == 2 && cp < 0x80) || (bytes == 3 && cp < 0x800) || (bytes == 4 && cp < 0x10000);
  const bool surrogate = (cp >= 0xD800 && cp <= 0xDFFF);
  if (overlong || surrogate || cp > 0x10FFFF) {
    (*string)++;
    return REPLACEMENT_GLYPH;
  }

  *string += bytes;

  return cp;
}

void utf8AppendCodepoint(uint32_t cp, std::string& out) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

int utf8SafeTruncateBuffer(const char* buf, int len) {
  if (len <= 0) return 0;

  // Walk back past continuation bytes (10xxxxxx) to find the lead byte
  int leadPos = len - 1;
  while (leadPos > 0 && (static_cast<uint8_t>(buf[leadPos]) & 0xC0) == 0x80) {
    leadPos--;
  }

  // Determine expected length of the sequence starting at leadPos
  int expectedLen = utf8CodepointLen(static_cast<unsigned char>(buf[leadPos]));
  int actualLen = len - leadPos;

  if (actualLen < expectedLen && leadPos > 0) {
    // Incomplete UTF-8 sequence at the end — exclude it
    return leadPos;
  }
  return len;
}

size_t utf8RemoveLastChar(std::string& str) {
  if (str.empty()) return 0;
  size_t pos = str.size() - 1;
  while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  str.resize(pos);
  return pos;
}

// Truncate string by removing N UTF-8 characters from the end
void utf8TruncateChars(std::string& str, const size_t numChars) {
  for (size_t i = 0; i < numChars && !str.empty(); ++i) {
    utf8RemoveLastChar(str);
  }
}

uint32_t utf8ToUpperCodepoint(const uint32_t cp) {
  // ASCII a-z
  if (cp >= 'a' && cp <= 'z') return cp - 0x20;

  // Latin-1 Supplement lowercase (à-þ), excluding ÷ (0xF7, not a letter). ß (0xDF)
  // has no single-codepoint uppercase, so it is left unchanged.
  if (cp >= 0x00E0 && cp <= 0x00FE && cp != 0x00F7) return cp - 0x20;
  if (cp == 0x00FF) return 0x0178;  // ÿ -> Ÿ

  // Latin Extended-A: parity differs per sub-block (upper is the even codepoint in
  // some ranges, the odd one in others).
  if (cp == 0x0131) return 0x0049;                                   // ı -> I
  if (cp == 0x017F) return 0x0053;                                   // ſ (long s) -> S
  if (cp >= 0x0100 && cp <= 0x0137) return (cp & 1u) ? cp - 1 : cp;  // even=upper (incl. ă 0x0103 -> 0x0102)
  if (cp >= 0x0139 && cp <= 0x0148) return (cp & 1u) ? cp : cp - 1;  // odd=upper
  if (cp >= 0x014A && cp <= 0x0177) return (cp & 1u) ? cp - 1 : cp;  // even=upper
  if (cp == 0x017A || cp == 0x017C || cp == 0x017E) return cp - 1;   // ź ż ž
  if (cp == 0x0153) return 0x0152;                                   // œ -> Œ

  // Latin Extended-B (Vietnamese horned vowels)
  if (cp == 0x01A1) return 0x01A0;  // ơ -> Ơ
  if (cp == 0x01B0) return 0x01AF;  // ư -> Ư

  // Greek (monotonic): α-ω, with final sigma mapping to Σ.
  if (cp == 0x03C2) return 0x03A3;  // ς -> Σ
  if (cp >= 0x03B1 && cp <= 0x03C9) return cp - 0x20;

  // Cyrillic
  if (cp >= 0x0430 && cp <= 0x044F) return cp - 0x20;
  if (cp >= 0x0450 && cp <= 0x045F) return cp - 0x50;

  // Latin Extended Additional (Vietnamese tone-marked letters): upper is the even
  // codepoint throughout 0x1E00-0x1EFF.
  if (cp >= 0x1E00 && cp <= 0x1EFF) return (cp & 1u) ? cp - 1 : cp;

  return cp;
}

std::string utf8ToUpper(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  const auto* p = reinterpret_cast<const unsigned char*>(in.c_str());
  uint32_t cp;
  while ((cp = utf8NextCodepoint(&p)) != 0) {
    // Non-letter and unsupported codepoints (including a literal U+FFFD from an
    // undecodable byte) map to themselves, so the output stays well-formed.
    utf8AppendCodepoint(utf8ToUpperCodepoint(cp), out);
  }
  return out;
}
