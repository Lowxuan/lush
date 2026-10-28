#pragma once

#include "lush/core/output.h"

namespace lush::cli {

// 把 Output 文本直接写到终端；不追加换行或缓冲。
class ConsoleSink final : public core::Output {
public:
    void write(std::string_view text) override;
};

} // namespace lush::cli
