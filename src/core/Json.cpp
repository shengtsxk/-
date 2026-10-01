#include "core/Json.h"
#include "core/PathUtils.h"

#include <sstream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <cctype>

namespace Lingjing {

    // ============================================================================
    // 构造
    // ============================================================================

    Json::Json() : type_(Type::Null) {}

    Json::Json(std::nullptr_t) : type_(Type::Null) {}

    Json::Json(bool value) : type_(Type::Bool), boolValue_(value) {}

    Json::Json(int32_t value) : type_(Type::Int), intValue_(value) {}

    Json::Json(int64_t value) : type_(Type::Int), intValue_(value) {}

    Json::Json(uint32_t value) : type_(Type::Int), intValue_(value) {}

    Json::Json(uint64_t value)
        : type_(Type::Int), intValue_(static_cast<int64_t>(value)) {
    }

    Json::Json(float value) : type_(Type::Float), floatValue_(value) {}

    Json::Json(double value) : type_(Type::Float), floatValue_(value) {}

    Json::Json(const char* value)
        : type_(Type::String), stringValue_(value ? value : "") {
    }

    Json::Json(const std::string& value)
        : type_(Type::String), stringValue_(value) {
    }

    Json::Json(const ArrayType& value)
        : type_(Type::Array), arrayValue_(value) {
    }

    Json::Json(const ObjectType& value)
        : type_(Type::Object), objectValue_(value) {
    }

    // ============================================================================
    // 类型访问
    // ============================================================================

    bool Json::asBool(bool defaultValue) const {
        if (type_ != Type::Bool) return defaultValue;
        return boolValue_;
    }

    int64_t Json::asInt(int64_t defaultValue) const {
        if (type_ == Type::Int) return intValue_;
        if (type_ == Type::Float) return static_cast<int64_t>(floatValue_);
        if (type_ == Type::Bool) return boolValue_ ? 1 : 0;
        return defaultValue;
    }

    uint64_t Json::asUint(uint64_t defaultValue) const {
        if (type_ == Type::Int) return static_cast<uint64_t>(intValue_);
        if (type_ == Type::Float) return static_cast<uint64_t>(floatValue_);
        return defaultValue;
    }

    double Json::asFloat(double defaultValue) const {
        if (type_ == Type::Float) return floatValue_;
        if (type_ == Type::Int) return static_cast<double>(intValue_);
        return defaultValue;
    }

    std::string Json::asString(const std::string& defaultValue) const {
        if (type_ == Type::String) return stringValue_;
        return defaultValue;
    }

    // ============================================================================
    // 数组访问
    // ============================================================================

    size_t Json::size() const {
        if (type_ == Type::Array) return arrayValue_.size();
        if (type_ == Type::Object) return objectValue_.size();
        return 0;
    }

    Json& Json::operator[](size_t index) {
        if (type_ != Type::Array) {
            type_ = Type::Array;
            arrayValue_.clear();
        }

        if (index >= arrayValue_.size()) {
            arrayValue_.resize(index + 1);
        }

        return arrayValue_[index];
    }

    const Json& Json::operator[](size_t index) const {
        static Json nullJson;

        if (type_ != Type::Array || index >= arrayValue_.size()) {
            return nullJson;
        }

        return arrayValue_[index];
    }

    void Json::push_back(const Json& value) {
        if (type_ != Type::Array) {
            type_ = Type::Array;
            arrayValue_.clear();
        }

        arrayValue_.push_back(value);
    }

    // ============================================================================
    // 对象访问
    // ============================================================================

    Json& Json::operator[](const std::string& key) {
        if (type_ != Type::Object) {
            type_ = Type::Object;
            objectValue_.clear();
        }

        return objectValue_[key];
    }

    const Json& Json::operator[](const std::string& key) const {
        static Json nullJson;

        if (type_ != Type::Object) return nullJson;

        auto it = objectValue_.find(key);
        return (it != objectValue_.end()) ? it->second : nullJson;
    }

    bool Json::has(const std::string& key) const {
        if (type_ != Type::Object) return false;
        return objectValue_.find(key) != objectValue_.end();
    }

    void Json::remove(const std::string& key) {
        if (type_ != Type::Object) return;
        objectValue_.erase(key);
    }

    // ============================================================================
    // 嵌套访问
    // ============================================================================

    Json& Json::setNested(const std::string& path, const Json& value) {
        auto parts = [&path]() {
            std::vector<std::string> result;
            std::string current;

            for (char c : path) {
                if (c == '.') {
                    if (!current.empty()) {
                        result.push_back(current);
                        current.clear();
                    }
                }
                else {
                    current += c;
                }
            }

            if (!current.empty()) result.push_back(current);
            return result;
            }();

        if (parts.empty()) return *this;

        Json* current = this;

        for (size_t i = 0; i < parts.size() - 1; ++i) {
            current = &(*current)[parts[i]];
        }

        (*current)[parts.back()] = value;
        return *current;
    }

    const Json* Json::getNested(const std::string& path) const {
        auto parts = [&path]() {
            std::vector<std::string> result;
            std::string current;

            for (char c : path) {
                if (c == '.') {
                    if (!current.empty()) {
                        result.push_back(current);
                        current.clear();
                    }
                }
                else {
                    current += c;
                }
            }

            if (!current.empty()) result.push_back(current);
            return result;
            }();

        const Json* current = this;

        for (const auto& key : parts) {
            if (current->type_ != Type::Object) return nullptr;

            auto it = current->objectValue_.find(key);

            if (it == current->objectValue_.end()) return nullptr;

            current = &it->second;
        }

        return current;
    }

    // ============================================================================
    // 序列化
    // ============================================================================

    static void escapeString(std::ostream& os, const std::string& s) {
        os << '"';

        for (char c : s) {
            switch (c) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\b': os << "\\b"; break;
            case '\f': os << "\\f"; break;
            case '\n': os << "\\n"; break;
            case '\r': os << "\\r"; break;
            case '\t': os << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    os << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<int>(c);
                }
                else {
                    os << c;
                }
            }
        }

        os << '"';
    }

    void Json::writeString(std::ostream& os, int indent,
        int currentIndent) const
    {
        auto newlineAndIndent = [&]() {
            if (indent >= 0) {
                os << '\n';
                os << std::string((currentIndent + 1) * indent, ' ');
            }
            };

        auto indentOnly = [&]() {
            if (indent >= 0) {
                os << std::string(currentIndent * indent, ' ');
            }
            };

        switch (type_) {
        case Type::Null:
            os << "null";
            break;

        case Type::Bool:
            os << (boolValue_ ? "true" : "false");
            break;

        case Type::Int:
            os << intValue_;
            break;

        case Type::Float: {
            if (std::isnan(floatValue_) || std::isinf(floatValue_)) {
                os << "null";
            }
            else {
                os << std::setprecision(10) << floatValue_;
            }
            break;
        }

        case Type::String:
            escapeString(os, stringValue_);
            break;

        case Type::Array: {
            if (arrayValue_.empty()) {
                os << "[]";
                break;
            }

            os << '[';

            for (size_t i = 0; i < arrayValue_.size(); ++i) {
                if (i > 0) os << ',';

                newlineAndIndent();
                arrayValue_[i].writeString(os, indent, currentIndent + 1);
            }

            if (indent >= 0) os << '\n';

            indentOnly();
            os << ']';
            break;
        }

        case Type::Object: {
            if (objectValue_.empty()) {
                os << "{}";
                break;
            }

            os << '{';

            size_t i = 0;

            for (const auto& [key, value] : objectValue_) {
                if (i > 0) os << ',';

                newlineAndIndent();
                escapeString(os, key);

                os << (indent >= 0 ? ": " : ":");

                value.writeString(os, indent, currentIndent + 1);

                ++i;
            }

            if (indent >= 0) os << '\n';

            indentOnly();
            os << '}';
            break;
        }
        }
    }

    std::string Json::toString(int indent) const {
        std::ostringstream oss;
        writeString(oss, indent, 0);
        return oss.str();
    }

    std::string Json::toCompactString() const {
        return toString(-1);
    }

    // ============================================================================
    // 解析
    // ============================================================================

    namespace {

        class Parser {
        public:
            Parser(const std::string& s) : s_(s), pos_(0) {}

            bool parse(Json& out) {
                skipWhitespace();

                if (!parseValue(out)) return false;

                skipWhitespace();

                return pos_ == s_.size();
            }

        private:
            bool parseValue(Json& out) {
                skipWhitespace();

                if (pos_ >= s_.size()) return false;

                char c = s_[pos_];

                if (c == 'n') {
                    if (s_.substr(pos_, 4) == "null") {
                        out = Json(nullptr);
                        pos_ += 4;
                        return true;
                    }
                    return false;
                }

                if (c == 't') {
                    if (s_.substr(pos_, 4) == "true") {
                        out = Json(true);
                        pos_ += 4;
                        return true;
                    }
                    return false;
                }

                if (c == 'f') {
                    if (s_.substr(pos_, 5) == "false") {
                        out = Json(false);
                        pos_ += 5;
                        return true;
                    }
                    return false;
                }

                if (c == '"') {
                    std::string str;
                    if (!parseString(str)) return false;
                    out = Json(str);
                    return true;
                }

                if (c == '[') {
                    return parseArray(out);
                }

                if (c == '{') {
                    return parseObject(out);
                }

                if (c == '-' || (c >= '0' && c <= '9')) {
                    return parseNumber(out);
                }

                return false;
            }

            bool parseString(std::string& out) {
                if (pos_ >= s_.size() || s_[pos_] != '"') return false;

                ++pos_;
                out.clear();

                while (pos_ < s_.size()) {
                    char c = s_[pos_];

                    if (c == '"') {
                        ++pos_;
                        return true;
                    }

                    if (c == '\\') {
                        ++pos_;

                        if (pos_ >= s_.size()) return false;

                        char esc = s_[pos_];

                        switch (esc) {
                        case '"':  out += '"'; break;
                        case '\\': out += '\\'; break;
                        case '/':  out += '/'; break;
                        case 'b':  out += '\b'; break;
                        case 'f':  out += '\f'; break;
                        case 'n':  out += '\n'; break;
                        case 'r':  out += '\r'; break;
                        case 't':  out += '\t'; break;
                        case 'u': {
                            if (pos_ + 4 >= s_.size()) return false;

                            int code = std::stoi(s_.substr(pos_ + 1, 4), nullptr, 16);

                            // 简化：仅处理 BMP
                            if (code < 0x80) {
                                out += static_cast<char>(code);
                            }
                            else if (code < 0x800) {
                                out += static_cast<char>(0xC0 | (code >> 6));
                                out += static_cast<char>(0x80 | (code & 0x3F));
                            }
                            else {
                                out += static_cast<char>(0xE0 | (code >> 12));
                                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                                out += static_cast<char>(0x80 | (code & 0x3F));
                            }

                            pos_ += 4;
                            break;
                        }
                        default:
                            return false;
                        }

                        ++pos_;
                    }
                    else {
                        out += c;
                        ++pos_;
                    }
                }

                return false;
            }

            bool parseNumber(Json& out) {
                size_t start = pos_;

                bool isFloat = false;

                if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;

                while (pos_ < s_.size() && std::isdigit(s_[pos_])) ++pos_;

                if (pos_ < s_.size() && s_[pos_] == '.') {
                    isFloat = true;
                    ++pos_;

                    while (pos_ < s_.size() && std::isdigit(s_[pos_])) ++pos_;
                }

                if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
                    isFloat = true;
                    ++pos_;

                    if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) {
                        ++pos_;
                    }

                    while (pos_ < s_.size() && std::isdigit(s_[pos_])) ++pos_;
                }

                std::string numStr = s_.substr(start, pos_ - start);

                if (numStr.empty()) return false;

                try {
                    if (isFloat) {
                        out = Json(std::stod(numStr));
                    }
                    else {
                        out = Json(static_cast<int64_t>(std::stoll(numStr)));
                    }
                    return true;
                }
                catch (...) {
                    return false;
                }
            }

            bool parseArray(Json& out) {
                if (pos_ >= s_.size() || s_[pos_] != '[') return false;

                ++pos_;
                out = Json(Json::ArrayType{});

                skipWhitespace();

                if (pos_ < s_.size() && s_[pos_] == ']') {
                    ++pos_;
                    return true;
                }

                while (pos_ < s_.size()) {
                    Json value;

                    if (!parseValue(value)) return false;

                    out.push_back(value);

                    skipWhitespace();

                    if (pos_ >= s_.size()) return false;

                    if (s_[pos_] == ',') {
                        ++pos_;
                        continue;
                    }

                    if (s_[pos_] == ']') {
                        ++pos_;
                        return true;
                    }

                    return false;
                }

                return false;
            }

            bool parseObject(Json& out) {
                if (pos_ >= s_.size() || s_[pos_] != '{') return false;

                ++pos_;
                out = Json(Json::ObjectType{});

                skipWhitespace();

                if (pos_ < s_.size() && s_[pos_] == '}') {
                    ++pos_;
                    return true;
                }

                while (pos_ < s_.size()) {
                    skipWhitespace();

                    if (pos_ >= s_.size() || s_[pos_] != '"') return false;

                    std::string key;

                    if (!parseString(key)) return false;

                    skipWhitespace();

                    if (pos_ >= s_.size() || s_[pos_] != ':') return false;

                    ++pos_;

                    Json value;

                    if (!parseValue(value)) return false;

                    out[key] = value;

                    skipWhitespace();

                    if (pos_ >= s_.size()) return false;

                    if (s_[pos_] == ',') {
                        ++pos_;
                        continue;
                    }

                    if (s_[pos_] == '}') {
                        ++pos_;
                        return true;
                    }

                    return false;
                }

                return false;
            }

            void skipWhitespace() {
                while (pos_ < s_.size() &&
                    std::isspace(static_cast<unsigned char>(s_[pos_]))) {
                    ++pos_;
                }
            }

            const std::string& s_;
            size_t pos_;
        };

    } // anonymous namespace

    bool Json::parse(const std::string& json) {
        Parser parser(json);
        return parser.parse(*this);
    }

    bool Json::parseFromFile(const std::string& path) {
        std::string content = PathUtils::readAllText(path);

        if (content.empty()) return false;

        return parse(content);
    }

    bool Json::saveToFile(const std::string& path, int indent) const {
        return PathUtils::writeAllText(path, toString(indent));
    }

    void Json::clear() {
        type_ = Type::Null;
        boolValue_ = false;
        intValue_ = 0;
        floatValue_ = 0.0;
        stringValue_.clear();
        arrayValue_.clear();
        objectValue_.clear();
    }

} // namespace Lingjing