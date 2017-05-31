#include <string>
using std::string;

struct Entry {
    bool is_in_use;
    string value;
};

// Return a number computed from the bits of the characters in the string s.
unsigned int hash_string(const string& s) {
    unsigned int h = 0;
    for (size_t i = 0; i < s.length(); i++)
        h += s[i];  // oh, how about we just add them all up
    return int(h);
}

const int TABLE_SIZE = 256;  // lookup() only works if this is a power of 2

class HashSet {
    Entry entries[TABLE_SIZE];   // hello, i'll be your hash table today
public:

    HashSet() {  // initialize the table
        for (int i = 0; i < TABLE_SIZE; i++)
            entries[i].is_in_use = false;
    }

    Entry* lookup(const string& value) {
        unsigned int hash = hash_string(value);
        unsigned int offset = hash % TABLE_SIZE;
        unsigned int step = ((hash / TABLE_SIZE) % TABLE_SIZE);
        step |= 1;  // Force step to be odd. That way the loop below eventually visits every entry in the table!

        for (int i = 0; i < TABLE_SIZE; i++) {
            Entry* e = &entries[offset];
            if (!e->is_in_use)
                return e;
            if (e->value == value)
                return e;

            offset = (offset + step) % TABLE_SIZE;  // no match found yet. move on.
        }
        return nullptr; // oh no, the hash table is full
    }

    bool contains(const string& value) {
        Entry* entry = lookup(value);
        return entry != nullptr && entry->is_in_use;
    }

    void add(const string& value) {
        Entry* entry = lookup(value);
        if (entry != nullptr && !entry->is_in_use) {
            entry->is_in_use = true;
            entry->value = value;
        }
    }
};
