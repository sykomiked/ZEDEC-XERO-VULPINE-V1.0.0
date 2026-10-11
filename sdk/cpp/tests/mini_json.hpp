// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// mini_json.hpp — just enough JSON to read sdk/conformance/vectors.json in
// the tests (objects, arrays, strings without escapes beyond \" \\ \/,
// integers, true/false/null). Numbers are kept as exact int64/uint64 text.
#ifndef ZXV_TEST_MINI_JSON_HPP
#define ZXV_TEST_MINI_JSON_HPP

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mini_json {

struct Value {
    enum Kind { null, boolean, number, string, array, object } kind = null;
    bool b = false;
    std::string text; // number literal or string contents
    std::vector<Value> items;
    std::map<std::string, Value> fields;

    const Value& operator[](const std::string& k) const
    {
        auto it = fields.find(k);
        if (it == fields.end()) throw std::runtime_error("missing key " + k);
        return it->second;
    }
    const Value& operator[](std::size_t i) const { return items.at(i); }
    std::size_t size() const { return kind == array ? items.size() : fields.size(); }
    std::int64_t i64() const { return std::stoll(text); }
    std::uint64_t u64() const { return std::stoull(text); }
    const std::string& str() const { return text; }
};

class Parser {
public:
    explicit Parser(std::string s) : s_(std::move(s)) {}
    Value parse()
    {
        Value v = value();
        ws();
        if (i_ != s_.size()) fail("trailing data");
        return v;
    }

private:
    [[noreturn]] void fail(const char* why) { throw std::runtime_error(std::string("json: ") + why); }
    void ws()
    {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t')) ++i_;
    }
    Value value()
    {
        ws();
        if (i_ >= s_.size()) fail("eof");
        Value v;
        char c = s_[i_];
        if (c == '{') {
            v.kind = Value::object;
            ++i_;
            ws();
            if (s_[i_] == '}') {
                ++i_;
                return v;
            }
            for (;;) {
                ws();
                std::string k = str();
                ws();
                if (s_[i_++] != ':') fail("colon");
                v.fields[k] = value();
                ws();
                if (s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (s_[i_++] != '}') fail("object end");
                return v;
            }
        }
        if (c == '[') {
            v.kind = Value::array;
            ++i_;
            ws();
            if (s_[i_] == ']') {
                ++i_;
                return v;
            }
            for (;;) {
                v.items.push_back(value());
                ws();
                if (s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (s_[i_++] != ']') fail("array end");
                return v;
            }
        }
        if (c == '"') {
            v.kind = Value::string;
            v.text = str();
            return v;
        }
        if (s_.compare(i_, 4, "true") == 0) {
            i_ += 4;
            v.kind = Value::boolean;
            v.b = true;
            return v;
        }
        if (s_.compare(i_, 5, "false") == 0) {
            i_ += 5;
            v.kind = Value::boolean;
            return v;
        }
        if (s_.compare(i_, 4, "null") == 0) {
            i_ += 4;
            return v;
        }
        v.kind = Value::number;
        std::size_t st = i_;
        while (i_ < s_.size() && (s_[i_] == '-' || (s_[i_] >= '0' && s_[i_] <= '9'))) ++i_;
        if (st == i_) fail("unexpected character");
        v.text = s_.substr(st, i_ - st);
        return v;
    }
    std::string str()
    {
        if (s_[i_++] != '"') fail("quote");
        std::string out;
        while (i_ < s_.size() && s_[i_] != '"') {
            if (s_[i_] == '\\') {
                ++i_;
                char e = s_[i_];
                out.push_back(e == 'n' ? '\n' : e == 't' ? '\t' : e);
            } else {
                out.push_back(s_[i_]);
            }
            ++i_;
        }
        ++i_;
        return out;
    }
    std::string s_;
    std::size_t i_ = 0;
};

inline Value load(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return Parser(ss.str()).parse();
}

} // namespace mini_json

#endif
