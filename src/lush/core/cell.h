#pragma once

#include "lush/core/function_table.h"
#include "lush/core/value.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lush::core {

class Cell : public std::enable_shared_from_this<Cell> {
public:
    Cell() = default;
    explicit Cell(Value value) : value_(std::move(value)) {}

    static std::shared_ptr<Cell> makeValue(Value value) {
        return std::make_shared<Cell>(std::move(value));
    }

    static std::shared_ptr<Cell> makeRef(std::shared_ptr<Cell> target) {
        auto cell = std::make_shared<Cell>();
        cell->ref_ = std::move(target);
        return cell;
    }

    bool isRef() const noexcept {
        return ref_ != nullptr;
    }

    const Value& read() const {
        if (ref_) {
            return ref_->read();
        }
        return *value_;
    }

    Value readCopy() const {
        return read();
    }

    Value& value() noexcept {
        return *value_;
    }

    const Value& value() const noexcept {
        return *value_;
    }

    void rebind(Value value) {
        value_ = std::move(value);
        ref_.reset();
    }

    void bindRef(std::shared_ptr<Cell> target) {
        ref_ = std::move(target);
        value_.reset();
    }

    std::shared_ptr<Cell> refTarget() const noexcept {
        return ref_;
    }

    std::shared_ptr<Cell> effective() {
        if (ref_) {
            return ref_;
        }
        return shared_from_this();
    }

    bool hasInboundRef() const noexcept {
        return hasInboundRef_;
    }

    void markInboundRef() noexcept {
        hasInboundRef_ = true;
    }

private:
    std::optional<Value> value_;
    std::shared_ptr<Cell> ref_;
    bool hasInboundRef_ = false;
};

class Env {
public:
    class ScopeGuard {
    public:
        ScopeGuard() = default;
        explicit ScopeGuard(Env& env) : env_(&env) {}
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;
        ScopeGuard(ScopeGuard&& other) noexcept : env_(other.env_) {
            other.env_ = nullptr;
        }
        ScopeGuard& operator=(ScopeGuard&& other) noexcept {
            if (this != &other) {
                if (env_) {
                    env_->popScope();
                }
                env_ = other.env_;
                other.env_ = nullptr;
            }
            return *this;
        }
        ~ScopeGuard() {
            if (env_) {
                env_->popScope();
            }
        }

    private:
        Env* env_ = nullptr;
    };

    Env() {
        scopes_.emplace_back();
    }

    Env(const Env&) = delete;
    Env& operator=(const Env&) = delete;

    ScopeGuard pushScope() {
        scopes_.emplace_back();
        return ScopeGuard(*this);
    }

    std::shared_ptr<Cell> find(std::string_view name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(std::string(name));
            if (found != it->end()) {
                return found->second;
            }
        }
        return nullptr;
    }

    bool exists(std::string_view name) const {
        return find(name) != nullptr;
    }

    std::shared_ptr<Cell> create(std::string_view name, Value value) {
        auto cell = Cell::makeValue(std::move(value));
        scopes_.back()[std::string(name)] = cell;
        return cell;
    }

    std::shared_ptr<Cell> createCell(std::string_view name,
                                     std::shared_ptr<Cell> cell) {
        scopes_.back()[std::string(name)] = std::move(cell);
        return scopes_.back()[std::string(name)];
    }

    void setCurrent(std::string_view name, std::shared_ptr<Cell> cell) {
        scopes_.back()[std::string(name)] = std::move(cell);
    }

    FunctionTable& functions() noexcept {
        return functions_;
    }

    const FunctionTable& functions() const noexcept {
        return functions_;
    }

    std::vector<std::string> visibleNames() const {
        std::vector<std::string> names;
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            for (const auto& entry : *it) {
                bool present = false;
                for (const std::string& name : names) {
                    if (name == entry.first) {
                        present = true;
                        break;
                    }
                }
                if (!present) {
                    names.push_back(entry.first);
                }
            }
        }
        return names;
    }

private:
    void popScope() {
        if (scopes_.size() > 1) {
            scopes_.pop_back();
        }
    }

    std::vector<std::unordered_map<std::string, std::shared_ptr<Cell>>> scopes_;
    FunctionTable functions_;
};

} // namespace lush::core
