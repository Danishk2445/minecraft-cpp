#include <gtest/gtest.h>

#include "app/ActionScript.h"

using namespace mc;

TEST(ActionScript, ParsesActionsLoopsAndComments) {
    std::string error;
    const auto program = parseActionScript(R"(# a comment line
log start here   # the rest of the line is a comment
until #minecraft:logs 5
  find #minecraft:logs 32
  mine
end
repeat 2
  craft #planks 4
end
smelt raw_iron 3 #minecraft:planks 2
expect diamond 1
)",
                                           &error);
    ASSERT_TRUE(program) << error;
    ASSERT_EQ(program->size(), 10u);
    const auto& p = *program;
    EXPECT_EQ(p[0].words, (std::vector<std::string>{"log", "start", "here"}));
    EXPECT_EQ(p[0].line, 2);
    // Loops: the head jumps past its end, the end back to its head.
    EXPECT_EQ(p[1].words[0], "until");
    EXPECT_EQ(p[1].jump, 4);
    EXPECT_EQ(p[4].words[0], "end");
    EXPECT_EQ(p[4].jump, 1);
    EXPECT_EQ(p[2].words, (std::vector<std::string>{"find", "#minecraft:logs", "32"}));
    EXPECT_EQ(p[5].jump, 7);
    EXPECT_EQ(p[7].jump, 5);
    EXPECT_EQ(p[6].words, (std::vector<std::string>{"craft", "#planks", "4"})); // a tag, not a comment
    EXPECT_EQ(p[8].words.size(), 5u);
    EXPECT_EQ(p[9].line, 11);
}

TEST(ActionScript, ReportsProblemsWithTheirLine) {
    const auto bad = [](std::string_view text) {
        std::string error;
        EXPECT_FALSE(parseActionScript(text, &error)) << text;
        return error;
    };
    EXPECT_EQ(bad("log ok\nfly away\n"), "line 2: unknown action 'fly'");
    EXPECT_EQ(bad("repeat 2\nmine\n"), "line 1: loop without end");
    EXPECT_EQ(bad("end\n"), "line 1: end without repeat or until");
    EXPECT_EQ(bad("find\n"), "line 1: find BLOCK|#TAG [RADIUS]");
    EXPECT_EQ(bad("craft stick lots\n"), "line 1: craft ITEM|#TAG [COUNT]");
    EXPECT_EQ(bad("smelt raw_iron 3 coal\n"), "line 1: smelt ITEM COUNT FUEL FUELCOUNT");
    EXPECT_EQ(bad("mine now\n"), "line 1: mine takes no arguments");
    EXPECT_EQ(bad("until diamond\nend\n"), "line 1: until ITEM COUNT");
}
