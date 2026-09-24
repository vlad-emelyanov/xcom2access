#include "vocab.h"

#include "game_ui.h"
#include "log.h"
#include "tts.h"
#include "ue3_reflection.h"

#include <atomic>
#include <string>
#include <unordered_map>

namespace Vocab {
namespace {

constexpr const char* kKeys[] = {
#include "vocab_words.inc"
};

std::unordered_map<std::string, std::string> sWords;
std::atomic<bool> sLoaded{false};
bool sAnnounced = false;

} // namespace

const char* Lookup(const char* key, const char* english)
{
    if (!sLoaded.load(std::memory_order_acquire))
        return english;
    const auto it = sWords.find(key);
    return it == sWords.end() ? english : it->second.c_str();
}

void LoadFromGame()
{
    if (sLoaded.load(std::memory_order_relaxed))
        return;
    void* cdo = GameUI::FindCompanionCDO("X2AccessVocab");
    if (!cdo)
        return;

    for (const char* key : kKeys) {
        std::string value = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), key);
        if (!value.empty())
            sWords.emplace(key, std::move(value));
    }
    std::string pluralRule = UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(cdo), "PluralRule");
    sWords.emplace("PluralRule", std::move(pluralRule));
    sLoaded.store(true, std::memory_order_release);
    LOG_INFO("Vocab: loaded %zu native words from X2AccessVocab", sWords.size());
}

void AnnounceStartup()
{
    if (!sAnnounced && sLoaded.load(std::memory_order_acquire)) {
        sAnnounced = true;
        TTS::Speak(std::string(StartupActive));
    }
}

} // namespace Vocab
