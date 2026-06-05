#include "LanguageRegistry.h"

#include <algorithm>
#include <array>

#include "HyphenationCommon.h"

// Per-language compile-time switches. Each Liang pattern trie is sizeable in flash
// (German alone is ~206 KB), so builds that don't need a given language can drop it
// to reclaim flash. Every language defaults to enabled, so the upstream/default
// build keeps the full set; a build only opts out by defining HYPHENATION_LANG_xx=0
// (see platformio.ini). Dropping a language makes getLanguageHyphenatorForPrimaryTag()
// return nullptr for it, which the reader already handles gracefully (it falls back to
// default min-prefix/suffix break rules, exactly as for languages with no trie at all).
#ifndef HYPHENATION_LANG_EN
#define HYPHENATION_LANG_EN 1
#endif
#ifndef HYPHENATION_LANG_FR
#define HYPHENATION_LANG_FR 1
#endif
#ifndef HYPHENATION_LANG_DE
#define HYPHENATION_LANG_DE 1
#endif
#ifndef HYPHENATION_LANG_RU
#define HYPHENATION_LANG_RU 1
#endif
#ifndef HYPHENATION_LANG_ES
#define HYPHENATION_LANG_ES 1
#endif
#ifndef HYPHENATION_LANG_IT
#define HYPHENATION_LANG_IT 1
#endif
#ifndef HYPHENATION_LANG_PL
#define HYPHENATION_LANG_PL 1
#endif
#ifndef HYPHENATION_LANG_SV
#define HYPHENATION_LANG_SV 1
#endif
#ifndef HYPHENATION_LANG_UK
#define HYPHENATION_LANG_UK 1
#endif

#if HYPHENATION_LANG_EN
#include "generated/hyph-en.trie.h"
#endif
#if HYPHENATION_LANG_FR
#include "generated/hyph-fr.trie.h"
#endif
#if HYPHENATION_LANG_DE
#include "generated/hyph-de.trie.h"
#endif
#if HYPHENATION_LANG_RU
#include "generated/hyph-ru.trie.h"
#endif
#if HYPHENATION_LANG_ES
#include "generated/hyph-es.trie.h"
#endif
#if HYPHENATION_LANG_IT
#include "generated/hyph-it.trie.h"
#endif
#if HYPHENATION_LANG_PL
#include "generated/hyph-pl.trie.h"
#endif
#if HYPHENATION_LANG_SV
#include "generated/hyph-sv.trie.h"
#endif
#if HYPHENATION_LANG_UK
#include "generated/hyph-uk.trie.h"
#endif

namespace {

#if HYPHENATION_LANG_EN
// English hyphenation patterns (3/3 minimum prefix/suffix length)
LanguageHyphenator englishHyphenator(en_patterns, isLatinLetter, toLowerLatin, 3, 3);
#endif
#if HYPHENATION_LANG_FR
LanguageHyphenator frenchHyphenator(fr_patterns, isLatinLetter, toLowerLatin);
#endif
#if HYPHENATION_LANG_DE
LanguageHyphenator germanHyphenator(de_patterns, isLatinLetter, toLowerLatin);
#endif
#if HYPHENATION_LANG_RU
LanguageHyphenator russianHyphenator(ru_patterns, isCyrillicLetter, toLowerCyrillic);
#endif
#if HYPHENATION_LANG_ES
LanguageHyphenator spanishHyphenator(es_patterns, isLatinLetter, toLowerLatin);
#endif
#if HYPHENATION_LANG_IT
LanguageHyphenator italianHyphenator(it_patterns, isLatinLetter, toLowerLatin);
#endif
#if HYPHENATION_LANG_SV
LanguageHyphenator swedishHyphenator(sv_patterns, isLatinLetter, toLowerLatin);
#endif
#if HYPHENATION_LANG_UK
LanguageHyphenator ukrainianHyphenator(uk_patterns, isCyrillicLetter, toLowerCyrillic);
#endif
#if HYPHENATION_LANG_PL
LanguageHyphenator polishHyphenator(pl_patterns, isLatinLetter, toLowerLatin);
#endif

constexpr size_t kLanguageCount = HYPHENATION_LANG_EN + HYPHENATION_LANG_FR + HYPHENATION_LANG_DE +
                                  HYPHENATION_LANG_RU + HYPHENATION_LANG_ES + HYPHENATION_LANG_IT +
                                  HYPHENATION_LANG_PL + HYPHENATION_LANG_SV + HYPHENATION_LANG_UK;

using EntryArray = std::array<LanguageEntry, kLanguageCount>;

const EntryArray& entries() {
  static const EntryArray kEntries = {{
#if HYPHENATION_LANG_EN
      {"english", "en", &englishHyphenator},
#endif
#if HYPHENATION_LANG_FR
      {"french", "fr", &frenchHyphenator},
#endif
#if HYPHENATION_LANG_DE
      {"german", "de", &germanHyphenator},
#endif
#if HYPHENATION_LANG_RU
      {"russian", "ru", &russianHyphenator},
#endif
#if HYPHENATION_LANG_ES
      {"spanish", "es", &spanishHyphenator},
#endif
#if HYPHENATION_LANG_IT
      {"italian", "it", &italianHyphenator},
#endif
#if HYPHENATION_LANG_PL
      {"polish", "pl", &polishHyphenator},
#endif
#if HYPHENATION_LANG_SV
      {"swedish", "sv", &swedishHyphenator},
#endif
#if HYPHENATION_LANG_UK
      {"ukrainian", "uk", &ukrainianHyphenator},
#endif
  }};
  return kEntries;
}

}  // namespace

const LanguageHyphenator* getLanguageHyphenatorForPrimaryTag(const std::string& primaryTag) {
  const auto& allEntries = entries();
  const auto it = std::find_if(allEntries.begin(), allEntries.end(),
                               [&primaryTag](const LanguageEntry& entry) { return primaryTag == entry.primaryTag; });
  return (it != allEntries.end()) ? it->hyphenator : nullptr;
}

LanguageEntryView getLanguageEntries() {
  const auto& allEntries = entries();
  return LanguageEntryView{allEntries.data(), allEntries.size()};
}
