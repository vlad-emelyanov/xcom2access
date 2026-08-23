#include "wire_record.hpp"

#include <cstring>

namespace X2Access::Wire
{

std::string RecField(const WireRecord& rec, const char* key)
{
    for (const auto& kv : rec) {
        if (kv.first == key)
            return kv.second;
    }
    return std::string();
}

std::vector<std::string> RecFieldAll(const WireRecord& rec, const char* key)
{
    std::vector<std::string> out;
    for (const auto& kv : rec) {
        if (kv.first == key)
            out.push_back(kv.second);
    }
    return out;
}

std::vector<std::string> SplitOn(const std::string& text, const char* sep)
{
    std::vector<std::string> parts;
    const size_t sepLen = strlen(sep);
    size_t start = 0;
    for (;;) {
        const size_t hit = text.find(sep, start);
        if (hit == std::string::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, hit - start));
        start = hit + sepLen;
    }
}

std::vector<WireRecord> ParseRecords(const std::string& raw, FieldCleaner clean)
{
    std::vector<WireRecord> records;
    if (raw.empty())
        return records;

    for (const std::string& recText : SplitOn(raw, kRecordSep)) {
        WireRecord rec;
        for (const std::string& field : SplitOn(recText, kFieldSep)) {
            const size_t eq = field.find('=');
            // ⛔ Malformed (no '=', or an empty key) is skipped; an empty VALUE is KEPT. See the
            // rule on ParseRecords — dropping one shifts every later value of a repeating field.
            if (eq == std::string::npos || eq == 0)
                continue;
            std::string value = field.substr(eq + 1);
            rec.emplace_back(field.substr(0, eq), clean ? clean(value) : std::move(value));
        }
        if (!rec.empty())
            records.push_back(std::move(rec));
    }
    return records;
}

} // namespace X2Access::Wire
