#pragma once

#include <string>
#include <string_view>

namespace lush::core {

// ASCII 大小写工具。
//
// 这里刻意只处理 'A'..'Z' / 'a'..'z'，不依赖 locale，也不改动非 ASCII
// 字节。词法关键字、内建名、命令名和用户函数名都使用这套规则。
//
// 参数：value — 单个字节；text — 待转换文本。
// 返回：小写后的字符/新字符串；asciiEqualsIgnoreCase 返回是否忽略 ASCII
//       大小写后相等。

char asciiLower(char value) noexcept;

std::string asciiLower(std::string_view text);

bool asciiEqualsIgnoreCase(std::string_view left,
                           std::string_view right) noexcept;

} // namespace lush::core
