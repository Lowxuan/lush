#pragma once

#include "lush/core/ast.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lush::core {
namespace detail {

inline std::string functionKey(std::string_view name) {
    std::string key;
    key.reserve(name.size());
    for (char character : name) {
        if (character >= 'A' && character <= 'Z') {
            key.push_back(static_cast<char>(character - 'A' + 'a'));
        } else {
            key.push_back(character);
        }
    }
    return key;
}

} // namespace detail

// Global, case-insensitive user-function signatures.  M8a installs signatures;
// M8b adds executable UserFunction values.
class FunctionTable {
public:
    bool insert(FunctionSignature signature, bool replace = false) {
        const std::string key = detail::functionKey(signature.name.text);
        const auto found = functions_.find(key);
        if (found != functions_.end() && !replace) {
            return false;
        }
        functions_.insert_or_assign(key, std::move(signature));
        return true;
    }

    const FunctionSignature* find(std::string_view name) const {
        const auto found = functions_.find(detail::functionKey(name));
        if (found == functions_.end()) {
            return nullptr;
        }
        return &found->second;
    }

    bool contains(std::string_view name) const {
        return find(name) != nullptr;
    }

    std::vector<FunctionSignature> signatures() const {
        std::vector<FunctionSignature> result;
        result.reserve(functions_.size());
        for (const auto& entry : functions_) {
            result.push_back(entry.second);
        }
        return result;
    }

    void clear() {
        functions_.clear();
    }

private:
    std::unordered_map<std::string, FunctionSignature> functions_;
};

} // namespace lush::core
