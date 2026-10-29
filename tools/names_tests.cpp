// Teams and Friends read name tags: the colour a tag starts with, and the name inside it.
#include "citron/names.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace citron::names;

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// The section sign as the game stores it, UTF-8.
std::string C(char code) { return std::string("\xC2\xA7") + code; }

}  // namespace

int main() {
    // the team is the first colour, whatever formatting comes before it
    Require(TeamOf(C('c') + "Steve") == 1, "red");
    Require(TeamOf(C('4') + "Steve") == 1, "dark red is still red");
    Require(TeamOf(C('9') + "Alex") == 2 && TeamOf(C('1') + "Alex") == 2, "blue and dark blue");
    Require(TeamOf(C('l') + C('a') + "Steve") == 3, "bold before the colour is skipped");
    Require(TeamOf(C('r') + C('k') + C('e') + "Steve") == 4, "reset and obfuscated are skipped");
    Require(TeamOf(C('A') + "Steve") == 3, "an upper-case code");
    Require(TeamOf("[" + C('6') + "VIP" + C('r') + "] " + C('b') + "Steve") == 8, "the first colour wins, even a rank's");
    Require(TeamOf("Steve") == 0, "no colour, no team");
    Require(TeamOf(C('l') + "Steve") == 0, "only formatting, no team");
    Require(TeamOf("Steve\xC2") == 0, "a cut off code at the end");
    Require(TeamOf(C('z') + "Steve") == 0, "a code that is no colour");
    Require(TeamOf(C('m') + "Steve") == TeamOf(C('c') + "Steve"), "Bedrock's material red is red");

    // what the tag reads as
    Require(Plain(C('c') + "Ste" + C('l') + "ve") == "Steve", "codes go");
    Require(Plain("  " + C('a') + "Steve  \nHealth 20") == "Steve", "first line, trimmed");
    Require(Plain("") == "" && Plain(C('a')) == "", "nothing left");

    // the name a friend entry gets
    Require(FriendName("[" + C('6') + "VIP] " + C('a') + "Steve") == "Steve", "a rank in brackets goes");
    Require(FriendName(C('7') + "(Red) Steve  Two") == "Steve Two", "a team in parentheses goes, spaces fold");
    Require(FriendName("Steve [Lv 12]") == "Steve", "a trailing group goes");
    Require(FriendName("Steve\nline two") == "Steve", "only the first line");

    // a whole name, not part of a longer one
    const std::vector<std::string> list = {"Steve", "x_Alex"};
    Require(OnList(list, "Steve"), "the name itself");
    Require(OnList(list, "[VIP] Steve"), "the name after a rank");
    Require(OnList(list, "steve"), "case does not matter");
    Require(OnList(list, "Team x_ALEX!"), "underscores are part of a name, punctuation is not");
    Require(!OnList(list, "Steve123"), "a longer name that starts with it");
    Require(!OnList(list, "BigSteve"), "a longer name that ends with it");
    Require(!OnList(list, "Alex"), "part of a listed name");
    Require(OnList(list, "BigSteve Steve"), "a later whole match counts");
    Require(!OnList({}, "Steve") && !OnList(list, ""), "an empty list or tag");
    Require(!OnList({""}, "Steve"), "an empty entry matches nothing");

    std::cout << "name tag tests passed\n";
    return 0;
}
