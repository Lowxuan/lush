#include "cli/console_sink.h"

#include <iostream>

namespace lush::cli {

void ConsoleSink::write(std::string_view text) {
    std::cout.write(text.data(), static_cast<std::streamsize>(text.size()));
    std::cout.flush();
}

} // namespace lush::cli
