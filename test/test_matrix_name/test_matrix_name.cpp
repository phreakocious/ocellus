#include <unity.h>
#include <string>
#include "../../matrix_name.h"

// Leading edge (t=0, brightest, bottom of the falling streak) is the name's LAST letter, so the
// column reads top-to-bottom as the name as it descends. Positions past the name return 0 -> the
// caller draws a random glyph there (the name is woven into an otherwise-normal rain column).
void test_name_glyph_reverse_anchored() {
    std::string n = "JOHN";
    TEST_ASSERT_EQUAL_CHAR('N', matrixNameGlyph(n, 0));   // leading edge = last letter
    TEST_ASSERT_EQUAL_CHAR('H', matrixNameGlyph(n, 1));
    TEST_ASSERT_EQUAL_CHAR('O', matrixNameGlyph(n, 2));
    TEST_ASSERT_EQUAL_CHAR('J', matrixNameGlyph(n, 3));   // top of the run = first letter
}

void test_name_glyph_beyond_name_is_random_sentinel() {
    std::string n = "JOHN";
    TEST_ASSERT_EQUAL_CHAR(0, matrixNameGlyph(n, 4));     // past the name -> random
    TEST_ASSERT_EQUAL_CHAR(0, matrixNameGlyph(n, -1));    // guard
    TEST_ASSERT_EQUAL_CHAR(0, matrixNameGlyph("", 0));    // empty name -> all random
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_name_glyph_reverse_anchored);
    RUN_TEST(test_name_glyph_beyond_name_is_random_sentinel);
    return UNITY_END();
}
