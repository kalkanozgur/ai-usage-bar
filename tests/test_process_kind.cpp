#include <gtest/gtest.h>

#include "core/process_kind.hpp"

using cub::process::classify;
using cub::process::Kind;

TEST(ProcessKind, DesktopInstalls) {
    EXPECT_EQ(classify(LR"(C:\Users\me\AppData\Local\AnthropicClaude\app-1.2.3\claude.exe)"),
              Kind::ClaudeDesktop);
    EXPECT_EQ(classify(LR"(C:\Program Files\WindowsApps\Claude_1.2.3.0_x64__pzs8sxrjxfjjc\app\Claude.exe)"),
              Kind::ClaudeDesktop);
}

TEST(ProcessKind, CliInstalls) {
    EXPECT_EQ(classify(LR"(C:\Users\me\.local\bin\claude.exe)"), Kind::ClaudeCli);
    EXPECT_EQ(classify(LR"(E:\tools\claude.exe)"), Kind::ClaudeCli);
}

TEST(ProcessKind, CustomDesktopHint) {
    EXPECT_EQ(classify(LR"(E:\Apps\MyClaude\claude.exe)", L"\\myclaude\\"), Kind::ClaudeDesktop);
}

TEST(ProcessKind, OtherProcesses) {
    EXPECT_EQ(classify(LR"(C:\Program Files\Google\Chrome\Application\chrome.exe)"), Kind::Other);
    EXPECT_EQ(classify(L""), Kind::Other);
    EXPECT_EQ(classify(LR"(C:\x\claude.exe.bak)"), Kind::Other);
}
