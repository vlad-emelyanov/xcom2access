#include "vocab.h"

#include "game_ui.h"
#include "log.h"
#include "tts.h"
#include "ue3_reflection.h"

#include <atomic>
#include <string>
#include <unordered_map>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Vocab {
namespace {

constexpr const char* kKeys[] = {
#include "vocab_words.inc"
};

std::unordered_map<std::string, std::string> sWords;
std::atomic<bool> sLoaded{false};
bool sAnnounced = false;
std::unordered_map<std::string, std::string> sGameWords;
std::unordered_map<std::string, uint64_t> sGameWordMissMs;

} // namespace

const char* Lookup(const char* key, const char* english)
{
    if (!sLoaded.load(std::memory_order_acquire))
        return english;
    const auto it = sWords.find(key);
    return it == sWords.end() ? english : it->second.c_str();
}

std::string GameString(const char* className, const char* propertyName, const Word& emergency)
{
    // Recipes call this on the game tick. A successful class-default read is immutable for the
    // session; a missing class is retried because TLE and mission classes load on demand.
    std::string key = std::string(className) + '.' + propertyName;
    if (const auto it = sGameWords.find(key); it != sGameWords.end())
        return it->second;
    const uint64_t now = GetTickCount64();
    const auto missed = sGameWordMissMs.find(key);
    if (missed == sGameWordMissMs.end() || now - missed->second >= 1000) {
        std::string value = GameUI::ReadGameDefaultString(className, propertyName);
        if (!value.empty()) {
            LOG_INFO("Vocab: game label %s resolved", key.c_str());
            sGameWords.emplace(std::move(key), value);
            return value;
        }
        if (missed == sGameWordMissMs.end())
            LOG_INFO("Vocab: game label %s not yet available", key.c_str());
        sGameWordMissMs[key] = now;
    }
    return emergency;
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
