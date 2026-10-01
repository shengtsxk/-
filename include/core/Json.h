#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <memory>
#include <cstdint>

namespace Lingjing {

    // ============================================================================
    // JSON 值
    // ============================================================================

    class Json {
    public:
        enum class Type {
            Null,
            Bool,
            Int,
            Float,
            String,
            Array,
            Object,
        };

        using ArrayType = std::vector<Json>;
        using ObjectType = std::map<std::string, Json>;

        // 构造
        Json();
        Json(std::nullptr_t);
        Json(bool value);
        Json(int32_t value);
        Json(int64_t value);
        Json(uint32_t value);
        Json(uint64_t value);
        Json(float value);
        Json(double value);
        Json(const char* value);
        Json(const std::string& value);
        Json(const ArrayType& value);
        Json(const ObjectType& value);

        // 类型查询
        Type type() const { return type_; }
        bool isNull() const { return type_ == Type::Null; }
        bool isBool() const { return type_ == Type::Bool; }
        bool isInt() const { return type_ == Type::Int; }
        bool isFloat() const { return type_ == Type::Float; }
        bool isNumber() const {
            return type_ == Type::Int || type_ == Type::Float;
        }
        bool isString() const { return type_ == Type::String; }
        bool isArray() const { return type_ == Type::Array; }
        bool isObject() const { return type_ == Type::Object; }

        // 访问（若类型不符，返回默认值）
        bool asBool(bool defaultValue = false) const;
        int64_t asInt(int64_t defaultValue = 0) const;
        uint64_t asUint(uint64_t defaultValue = 0) const;
        double asFloat(double defaultValue = 0.0) const;
        std::string asString(const std::string& defaultValue = "") const;

        // 数组访问
        size_t size() const;
        Json& operator[](size_t index);
        const Json& operator[](size_t index) const;
        void push_back(const Json& value);

        // 对象访问
        Json& operator[](const std::string& key);
        const Json& operator[](const std::string& key) const;
        bool has(const std::string& key) const;
        void remove(const std::string& key);

        // 迭代
        ArrayType& array() { return arrayValue_; }
        const ArrayType& array() const { return arrayValue_; }
        ObjectType& objects() { return objectValue_; }
        const ObjectType& objects() const { return objectValue_; }

        // 嵌套访问（"a.b.c"）
        Json& setNested(const std::string& path, const Json& value);
        const Json* getNested(const std::string& path) const;

        // 序列化
        std::string toString(int indent = -1) const;
        std::string toCompactString() const;

        // 解析
        bool parse(const std::string& json);
        bool parseFromFile(const std::string& path);

        // 保存
        bool saveToFile(const std::string& path, int indent = 2) const;

        // 清空
        void clear();

    private:
        void writeString(std::ostream& os, int indent, int currentIndent) const;
        bool parseValue(const std::string& s, size_t& pos);

        Type type_ = Type::Null;

        bool boolValue_ = false;
        int64_t intValue_ = 0;
        double floatValue_ = 0.0;
        std::string stringValue_;
        ArrayType arrayValue_;
        ObjectType objectValue_;
    };

} // namespace Lingjing