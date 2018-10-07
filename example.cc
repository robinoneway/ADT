#include <iostream>
#include "hashmap.h"

void TestHashMap() {
    SmallHashMap<char, int> letter_counts;
    letter_counts['a'] = 27;
    letter_counts['b'] = 3;
    letter_counts['c'] = 1;

    std::cout << "initially:\n";
    for (const auto &pair : letter_counts) {
        std::cout << pair.first << ": " << pair.second << '\n';
    }

    letter_counts['b'] = 42;  // update an existing value
    letter_counts['x'] = 9;  // insert a new value

    std::cout << "after modifications:\n";
    for (const auto &pair : letter_counts) {
        std::cout << pair.first << ": " << pair.second << '\n';
    }

    // count the number of occurrences of each word
    // (the first call to operator[] initialized the counter with zero)
    SmallHashMap<std::string, size_t>  word_map;
    for (const auto &w : { "this", "sentence", "is", "not", "a", "sentence",
                           "this", "sentence", "is", "a", "hoax"}) {
        ++word_map[w];
    }

    for (const auto &pair : word_map) {
        std::cout << pair.second << " occurrences of word '" << pair.first << "'\n";
    }
}

int main() {
    TestHashMap();
    return 0;
}
