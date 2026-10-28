#pragma once

#include "lush/core/ast.h"
#include "lush/core/text.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lush::core {

// Env 级用户函数签名表。
//
// 键是 ASCII 小写化后的函数名，因此查找和重定义判断都不区分大小写；
// 值在 M8a 只有签名，M8b 起会扩展为可执行的用户函数。
//
// 该表不拥有 Source/AST；M8a 的函数体尚未执行，M8b 会把 Source/AST
// 生命周期统一交给 Env。
class FunctionTable {
public:
    // 插入一个函数签名。
    // 参数：signature — 待插入签名；replace — 同名时是否覆盖。
    // 返回：成功插入/覆盖为 true；同名且 replace=false 为 false。
    bool insert(FunctionSignature signature, bool replace = false) {
        const std::string key = asciiLower(signature.name.text);
        const auto found = functions_.find(key);
        if (found != functions_.end() && !replace) {
            return false;
        }
        functions_.insert_or_assign(key, std::move(signature));
        return true;
    }

    // 按函数名查找签名；大小写不敏感。未找到返回 nullptr。
    const FunctionSignature* find(std::string_view name) const {
        const auto found = functions_.find(asciiLower(name));
        if (found == functions_.end()) {
            return nullptr;
        }
        return &found->second;
    }

    // 判断函数名是否已存在；大小写不敏感。
    bool contains(std::string_view name) const {
        return find(name) != nullptr;
    }

    // 返回所有签名的快照，顺序未定义；调用方不应依赖顺序。
    std::vector<FunctionSignature> signatures() const {
        std::vector<FunctionSignature> result;
        result.reserve(functions_.size());
        for (const auto& entry : functions_) {
            result.push_back(entry.second);
        }
        return result;
    }

    // 清空全部签名。
    void clear() {
        functions_.clear();
    }

private:
    std::unordered_map<std::string, FunctionSignature> functions_;
};

} // namespace lush::core
