#include "test_harness.h"

#include "lush/lush.h"

#include <string>

LUSH_TEST(version_is_not_empty) {
    LUSH_CHECK(!lush::version().empty());
}

LUSH_TEST(version_matches_header) {
    LUSH_CHECK_EQ(std::string(lush::version()), std::string(lush::kVersion));
}
