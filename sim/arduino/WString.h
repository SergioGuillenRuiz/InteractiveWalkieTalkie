// Host mock of the Arduino String class (backed by std::string).
// Cubre exactamente la API que usa el firmware del proyecto.
#ifndef SIM_WSTRING_H
#define SIM_WSTRING_H

#include <string>
#include <cstdio>
#include <cstdint>

class String {
public:
    std::string s;

    String() {}
    String(const char *c) : s(c ? c : "") {}
    String(const std::string &str) : s(str) {}
    String(const String &o) : s(o.s) {}
    explicit String(char c) { s = std::string(1, c); }
    explicit String(unsigned char c) { char b[16]; snprintf(b, sizeof(b), "%u", (unsigned)c); s = b; }
    explicit String(int n, int base = 10) { format_int((long)n, base); }
    explicit String(unsigned int n, int base = 10) { format_uint((unsigned long)n, base); }
    explicit String(long n, int base = 10) { format_int(n, base); }
    explicit String(unsigned long n, int base = 10) { format_uint(n, base); }

    String &operator=(const String &o) { s = o.s; return *this; }
    String &operator=(const char *c) { s = (c ? c : ""); return *this; }

    unsigned int length() const { return (unsigned int)s.size(); }
    bool isEmpty() const { return s.empty(); }
    const char *c_str() const { return s.c_str(); }

    char charAt(unsigned int i) const { return i < s.size() ? s[i] : 0; }
    char operator[](unsigned int i) const { return i < s.size() ? s[i] : 0; }
    char &operator[](unsigned int i) { return s[i]; }

    String substring(unsigned int from) const {
        if (from >= s.size()) return String();
        return String(s.substr(from));
    }
    String substring(unsigned int from, unsigned int to) const {
        if (from >= s.size() || to <= from) return String();
        if (to > s.size()) to = (unsigned int)s.size();
        return String(s.substr(from, to - from));
    }

    int indexOf(char c) const { auto p = s.find(c); return p == std::string::npos ? -1 : (int)p; }
    int indexOf(char c, unsigned int from) const {
        auto p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
    int indexOf(const String &o) const { auto p = s.find(o.s); return p == std::string::npos ? -1 : (int)p; }

    bool startsWith(const String &o) const { return s.rfind(o.s, 0) == 0; }
    bool startsWith(const char *c) const { std::string t(c); return s.rfind(t, 0) == 0; }

    String &operator+=(const String &o) { s += o.s; return *this; }
    String &operator+=(const char *c) { if (c) s += c; return *this; }
    String &operator+=(char c) { s += c; return *this; }

    bool operator==(const String &o) const { return s == o.s; }
    bool operator==(const char *c) const { return s == (c ? c : ""); }
    bool operator!=(const String &o) const { return s != o.s; }
    bool operator!=(const char *c) const { return s != (c ? c : ""); }

private:
    void format_int(long n, int base) {
        char b[32];
        if (base == 16) snprintf(b, sizeof(b), "%lX", n);
        else if (base == 2) { // binario simple
            std::string r; unsigned long v = (unsigned long)n; if (!v) r = "0";
            while (v) { r = char('0' + (v & 1)) + r; v >>= 1; } s = r; return;
        } else snprintf(b, sizeof(b), "%ld", n);
        s = b;
    }
    void format_uint(unsigned long n, int base) {
        char b[32];
        if (base == 16) snprintf(b, sizeof(b), "%lX", n);
        else snprintf(b, sizeof(b), "%lu", n);
        s = b;
    }
};

inline String operator+(const String &a, const String &b) { String r(a); r += b; return r; }
inline String operator+(const String &a, const char *b)   { String r(a); r += b; return r; }
inline String operator+(const char *a, const String &b)   { String r(a); r += b; return r; }
inline String operator+(const String &a, char b)          { String r(a); r += b; return r; }
inline bool operator==(const char *a, const String &b) { return b == a; }
inline bool operator!=(const char *a, const String &b) { return b != a; }

#endif // SIM_WSTRING_H
