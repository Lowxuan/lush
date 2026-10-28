#pragma once

#include "lush/core/value.h"

#include <string>
#include <string_view>

// 语言级值格式化；与 AST dump 的文本格式无关。
namespace lush::core {

// 顶层格式：字符串不加引号，供 echo/string 使用。
std::string formatValue(const Value& value);

// 嵌套格式：字符串加引号并转义，供 list/point 元素使用。
std::string formatValueNested(const Value& value);

std::string formatDouble(double value);
std::string escapeString(std::string_view text);

} // namespace lush::core
