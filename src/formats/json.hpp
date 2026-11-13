#pragma once
// A small JSON reader (objects, arrays, strings with escapes, numbers, true/false/null),
// depth-limited (16). Enough for the art pack manifest (formats/artpack_manifest.hpp) and the
// GitHub release info (formats/release_info.hpp). Pure (no OS calls).

#include <map>
#include <string>
#include <vector>

namespace d2k::formats::json {

struct Value {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Value> arr;
    std::map<std::string, Value> obj;
};

// The whole text must be one JSON value (whitespace around it allowed). False + `error` otherwise.
bool parse(const std::string& text, Value& out, std::string& error);

// Object member `key` (nullptr when missing or `o` is not an object).
const Value* field(const Value& o, const char* key);
// A string member ("" when missing or not a string).
std::string str(const Value& o, const char* key);

}  // namespace d2k::formats::json
