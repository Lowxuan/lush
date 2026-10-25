#pragma once

#include "lush/driver/driver.h"

namespace lush::cli {

bool stdinIsTty();

// Runs a line-oriented REPL. Returns the process exit code.
int runRepl(driver::Driver& driver);

} // namespace lush::cli
