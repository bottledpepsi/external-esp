#include "Json.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace json
{
    namespace
    {
        struct Parser
        {
            std::string_view t;
            size_t i = 0;
            std::string err;

            bool fail(const char* m)
            {
                if (err.empty()) err = m;
                return false;
            }

            void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i; }

            bool eat(char c)
            {
                ws();
                if (i < t.size() && t[i] == c)
                {
                    ++i;
                    return true;
                }
                return false;
            }

            static void appendUtf8(std::string& o, unsigned cp)
            {
                if (cp < 0x80) o += static_cast<char>(cp);
                else if (cp < 0x800)
                {
                    o += static_cast<char>(0xC0 | (cp >> 6));
                    o += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else if (cp < 0x10000)
                {
                    o += static_cast<char>(0xE0 | (cp >> 12));
                    o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    o += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else
                {
                    o += static_cast<char>(0xF0 | (cp >> 18));
                    o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    o += static_cast<char>(0x80 | (cp & 0x3F));
                }
            }

            bool str(std::string& out)
            {
                ws();
                if (i >= t.size() || t[i] != '"') return fail("expected string");
                ++i;
                out.clear();
                while (i < t.size())
                {
                    char c = t[i++];
                    if (c == '"') return true;
                    if (c != '\\')
                    {
                        out += c;
                        continue;
                    }
                    if (i >= t.size()) break;
                    char e = t[i++];
                    switch (e)
                    {
                    case 'n': out += '\n';
                        break;
                    case 't': out += '\t';
                        break;
                    case 'r': out += '\r';
                        break;
                    case 'b': out += '\b';
                        break;
                    case 'f': out += '\f';
                        break;
                    case 'u':
                        {
                            if (i + 4 > t.size()) return fail("bad \\u escape");
                            unsigned cp = static_cast<unsigned>(std::strtoul(
                                std::string(t.substr(i, 4)).c_str(), nullptr, 16));
                            i += 4;
                            if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u')
                            {
                                unsigned lo = static_cast<unsigned>(std::strtoul(
                                    std::string(t.substr(i + 2, 4)).c_str(), nullptr, 16));
                                i += 6;
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            }
                            appendUtf8(out, cp);
                            break;
                        }
                    default: out += e;
                        break;
                    }
                }
                return fail("unterminated string");
            }

            bool skipValue()
            {
                // any JSON value (used for nested things we do not model)
                ws();
                if (i >= t.size()) return fail("unexpected end");
                char c = t[i];
                if (c == '"')
                {
                    std::string tmp;
                    return str(tmp);
                }
                if (c == '{' || c == '[')
                {
                    char open = c, close = c == '{' ? '}' : ']';
                    int depth = 0;
                    bool inStr = false;
                    for (; i < t.size(); ++i)
                    {
                        char d = t[i];
                        if (inStr)
                        {
                            if (d == '\\') ++i;
                            else if (d == '"') inStr = false;
                            continue;
                        }
                        if (d == '"') inStr = true;
                        else if (d == open) ++depth;
                        else if (d == close && --depth == 0)
                        {
                            ++i;
                            return true;
                        }
                    }
                    return fail("unterminated container");
                }
                while (i < t.size() && t[i] != ',' && t[i] != '}' && t[i] != ']') ++i; // number / literal
                return true;
            }

            bool value(Value& v)
            {
                ws();
                const size_t start = i;
                if (!valueImpl(v)) return false;
                v.raw.assign(t.substr(start, i - start));
                return true;
            }

            bool valueImpl(Value& v)
            {
                ws();
                if (i >= t.size()) return fail("unexpected end");
                char c = t[i];
                if (c == '"')
                {
                    v.type = Value::Type::String;
                    return str(v.s);
                }
                if (t.substr(i, 4) == "true")
                {
                    i += 4;
                    v.type = Value::Type::Bool;
                    v.b = true;
                    return true;
                }
                if (t.substr(i, 5) == "false")
                {
                    i += 5;
                    v.type = Value::Type::Bool;
                    v.b = false;
                    return true;
                }
                if (t.substr(i, 4) == "null")
                {
                    i += 4;
                    v.type = Value::Type::Null;
                    return true;
                }
                if (c == '{' || c == '[')
                {
                    v.type = Value::Type::Null;
                    return skipValue();
                }
                if (c == '-' || (c >= '0' && c <= '9'))
                {
                    char* end = nullptr;
                    std::string tmp(t.substr(i, 40));
                    v.n = std::strtod(tmp.c_str(), &end);
                    if (end == tmp.c_str()) return fail("bad number");
                    i += static_cast<size_t>(end - tmp.c_str());
                    // "-inf", "-nan" and "1e999" are accepted by strtod but are no numbers: the value becomes Null (the key then counts as
                    // "not a number" and the caller reports it), instead of an infinity that turns into undefined behaviour at the first int cast
                    v.type = std::isfinite(v.n) ? Value::Type::Number : Value::Type::Null;
                    return true;
                }
                return fail("unexpected character");
            }
        };
    } // namespace

    std::optional<Object> ParseObject(std::string_view text, std::string* error)
    {
        Parser p;
        p.t = text;
        Object o;
        if (!p.eat('{'))
        {
            if (error) *error = "expected '{'";
            return std::nullopt;
        }
        if (p.eat('}')) return o;
        for (;;)
        {
            std::string key;
            Value v;
            if (!p.str(key) || !p.eat(':') || !p.value(v))
            {
                if (error) *error = p.err.empty() ? "malformed object" : p.err;
                return std::nullopt;
            }
            o[key] = std::move(v);
            if (p.eat(',')) continue;
            if (p.eat('}')) return o;
            if (error) *error = "expected ',' or '}'";
            return std::nullopt;
        }
    }

    std::optional<std::vector<std::string>> ParseStringArray(std::string_view text, std::string* error)
    {
        Parser p;
        p.t = text;
        std::vector<std::string> out;
        if (!p.eat('['))
        {
            if (error) *error = "expected '['";
            return std::nullopt;
        }
        if (p.eat(']')) return out;
        for (;;)
        {
            p.ws();
            if (p.i < text.size() && text[p.i] == '"')
            {
                std::string s;
                if (!p.str(s))
                {
                    if (error) *error = p.err;
                    return std::nullopt;
                }
                out.push_back(std::move(s));
            }
            else if (!p.skipValue())
            {
                if (error) *error = p.err;
                return std::nullopt;
            }
            if (p.eat(',')) continue;
            if (p.eat(']')) return out;
            if (error) *error = "expected ',' or ']'";
            return std::nullopt;
        }
    }

    namespace
    {
        struct Strict
        {
            std::string_view t;
            size_t i = 0;

            void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i; }
            bool lit(const char* w)
            {
                const size_t n = std::char_traits<char>::length(w);
                if (t.substr(i, n) != w) return false;
                i += n;
                return true;
            }
            static bool digit(char c) { return c >= '0' && c <= '9'; }
            bool number()
            {
                if (i < t.size() && t[i] == '-') ++i;
                if (i >= t.size() || !digit(t[i])) return false;
                if (t[i] == '0') ++i;
                else while (i < t.size() && digit(t[i])) ++i;
                if (i < t.size() && t[i] == '.')
                {
                    ++i;
                    if (i >= t.size() || !digit(t[i])) return false;
                    while (i < t.size() && digit(t[i])) ++i;
                }
                if (i < t.size() && (t[i] == 'e' || t[i] == 'E'))
                {
                    ++i;
                    if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i;
                    if (i >= t.size() || !digit(t[i])) return false;
                    while (i < t.size() && digit(t[i])) ++i;
                }
                return true;
            }
            bool string()
            {
                if (i >= t.size() || t[i] != '"') return false;
                ++i;
                while (i < t.size())
                {
                    const unsigned char c = static_cast<unsigned char>(t[i++]);
                    if (c == '"') return true;
                    if (c < 0x20) return false;
                    if (c != '\\') continue;
                    if (i >= t.size()) return false;
                    const char e = t[i++];
                    if (e == 'u')
                    {
                        for (int k = 0; k < 4; ++k, ++i)
                            if (i >= t.size() || !std::isxdigit(static_cast<unsigned char>(t[i]))) return false;
                    }
                    else if (std::string_view("\"\\/bfnrt").find(e) == std::string_view::npos) return false;
                }
                return false;
            }
            bool value(int depth)
            {
                if (depth > 64) return false;
                ws();
                if (i >= t.size()) return false;
                const char c = t[i];
                if (c == '"') return string();
                if (c == '{')
                {
                    ++i;
                    ws();
                    if (i < t.size() && t[i] == '}') { ++i; return true; }
                    for (;;)
                    {
                        ws();
                        if (!string()) return false;
                        ws();
                        if (i >= t.size() || t[i++] != ':') return false;
                        if (!value(depth + 1)) return false;
                        ws();
                        if (i >= t.size()) return false;
                        if (t[i] == ',') { ++i; continue; }
                        if (t[i] == '}') { ++i; return true; }
                        return false;
                    }
                }
                if (c == '[')
                {
                    ++i;
                    ws();
                    if (i < t.size() && t[i] == ']') { ++i; return true; }
                    for (;;)
                    {
                        if (!value(depth + 1)) return false;
                        ws();
                        if (i >= t.size()) return false;
                        if (t[i] == ',') { ++i; continue; }
                        if (t[i] == ']') { ++i; return true; }
                        return false;
                    }
                }
                if (c == 't') return lit("true");
                if (c == 'f') return lit("false");
                if (c == 'n') return lit("null");
                return number();
            }
        };
    } // namespace

    bool IsValid(std::string_view text)
    {
        Strict s;
        s.t = text;
        if (!s.value(0)) return false;
        s.ws();
        return s.i == text.size();
    }

    std::string Escape(std::string_view s)
    {
        std::string o = "\"";
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"': o += "\\\"";
                break;
            case '\\': o += "\\\\";
                break;
            case '\n': o += "\\n";
                break;
            case '\r': o += "\\r";
                break;
            case '\t': o += "\\t";
                break;
            default:
                if (c < 0x20)
                {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                }
                else o += static_cast<char>(c);
            }
        }
        return o + "\"";
    }

    std::string Number(double v)
    {
        if (std::isnan(v) || std::isinf(v)) return "0";
        char b[40];
        std::snprintf(b, sizeof b, "%.6g", v);
        return b;
    }

    std::string GetString(const Object& o, const char* k, const std::string& fb)
    {
        auto it = o.find(k);
        return it != o.end() && it->second.type == Value::Type::String ? it->second.s : fb;
    }

    double GetNumber(const Object& o, const char* k, double fb)
    {
        auto it = o.find(k);
        return it != o.end() && it->second.type == Value::Type::Number ? it->second.n : fb;
    }

    bool GetBool(const Object& o, const char* k, bool fb)
    {
        auto it = o.find(k);
        return it != o.end() && it->second.type == Value::Type::Bool ? it->second.b : fb;
    }
} // namespace json
