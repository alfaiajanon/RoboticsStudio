#include "Arduino.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <strings.h>

void String::assign(const char* s, unsigned int n) {
    char* fresh = static_cast<char*>(malloc(n + 1));
    if (!fresh) return;   // out of memory: keep the old contents, as Arduino does
    if (n) memcpy(fresh, s, n);
    fresh[n] = '\0';
    free(buf);
    buf = fresh;
    len = n;
}

void String::append(const char* s, unsigned int n) {
    if (!n) return;
    char* grown = static_cast<char*>(realloc(buf, len + n + 1));
    if (!grown) return;
    memmove(grown + len, s, n);   // s may point into buf
    buf = grown;
    len += n;
    buf[len] = '\0';
}

static String fromFormat(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static String fromFormat(const char* fmt, ...) {
    char tmp[48];
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, args);
    va_end(args);
    return String(tmp);
}

static String fromInteger(unsigned long v, unsigned char base, bool negative) {
    char tmp[8 * sizeof(long) + 2];
    char* p = &tmp[sizeof(tmp) - 1];
    *p = '\0';
    if (base < 2 || base > 36) base = 10;
    do {
        const unsigned long digit = v % base;
        v /= base;
        *--p = char(digit < 10 ? '0' + digit : 'a' + digit - 10);
    } while (v);
    if (negative) *--p = '-';
    return String(p);
}

String::String(const char* s) { assign(s ? s : "", s ? unsigned(strlen(s)) : 0); }
String::String(const String& other) { assign(other.c_str(), other.len); }
String::String(String&& other) noexcept : buf(other.buf), len(other.len) { other.buf = nullptr; other.len = 0; }
String::String(char c) { assign(&c, 1); }
String::String(int value, unsigned char base) : String(long(value), base) {}
String::String(unsigned int value, unsigned char base) : String((unsigned long)value, base) {}
String::String(long value, unsigned char base)
    : String(base == 10 && value < 0 ? fromInteger(0UL - (unsigned long)value, 10, true)
                                     : fromInteger((unsigned long)value, base, false)) {}
String::String(unsigned long value, unsigned char base) : String(fromInteger(value, base, false)) {}
String::String(float value, unsigned char decimals) : String(double(value), decimals) {}
String::String(double value, unsigned char decimals) : String(fromFormat("%.*f", int(decimals), value)) {}
String::~String() { free(buf); }

String& String::operator=(const String& other) {
    if (this != &other) assign(other.c_str(), other.len);
    return *this;
}

String& String::operator=(String&& other) noexcept {
    if (this != &other) {
        free(buf);
        buf = other.buf;
        len = other.len;
        other.buf = nullptr;
        other.len = 0;
    }
    return *this;
}

String& String::operator=(const char* s) {
    assign(s ? s : "", s ? unsigned(strlen(s)) : 0);
    return *this;
}

String& String::operator+=(const char* s) {
    if (s) append(s, unsigned(strlen(s)));
    return *this;
}

String& String::operator+=(char c) {
    append(&c, 1);
    return *this;
}

char& String::operator[](unsigned int i) {
    static char dummy;
    if (i >= len) { dummy = 0; return dummy; }
    return buf[i];
}

bool String::equals(const String& s) const { return len == s.len && strcmp(c_str(), s.c_str()) == 0; }
bool String::equals(const char* s) const { return strcmp(c_str(), s ? s : "") == 0; }
bool String::equalsIgnoreCase(const String& s) const { return len == s.len && strcasecmp(c_str(), s.c_str()) == 0; }
int String::compareTo(const String& s) const { return strcmp(c_str(), s.c_str()); }
bool String::startsWith(const String& s) const { return s.len <= len && strncmp(c_str(), s.c_str(), s.len) == 0; }
bool String::endsWith(const String& s) const { return s.len <= len && strcmp(c_str() + len - s.len, s.c_str()) == 0; }

int String::indexOf(char c, unsigned int from) const {
    if (from >= len) return -1;
    const char* hit = strchr(c_str() + from, c);
    return hit ? int(hit - c_str()) : -1;
}

int String::indexOf(const String& s, unsigned int from) const {
    if (from > len) return -1;
    const char* hit = strstr(c_str() + from, s.c_str());
    return hit ? int(hit - c_str()) : -1;
}

int String::lastIndexOf(char c) const {
    const char* hit = strrchr(c_str(), c);
    return hit ? int(hit - c_str()) : -1;
}

String String::substring(unsigned int from, unsigned int to) const {
    if (from > to) { unsigned int t = from; from = to; to = t; }
    if (from >= len) return String();
    if (to > len) to = len;
    String out;
    out.assign(c_str() + from, to - from);
    return out;
}

long String::toInt() const { return atol(c_str()); }
float String::toFloat() const { return float(atof(c_str())); }
double String::toDouble() const { return atof(c_str()); }

void String::trim() {
    if (!len) return;
    unsigned int begin = 0, end = len;
    while (begin < end && isspace((unsigned char)buf[begin])) ++begin;
    while (end > begin && isspace((unsigned char)buf[end - 1])) --end;
    memmove(buf, buf + begin, end - begin);
    len = end - begin;
    buf[len] = '\0';
}

void String::toUpperCase() { for (unsigned int i = 0; i < len; ++i) buf[i] = char(toupper((unsigned char)buf[i])); }
void String::toLowerCase() { for (unsigned int i = 0; i < len; ++i) buf[i] = char(tolower((unsigned char)buf[i])); }

void String::replace(char find, char with) {
    for (unsigned int i = 0; i < len; ++i) if (buf[i] == find) buf[i] = with;
}

void String::remove(unsigned int index, unsigned int count) {
    if (index >= len) return;
    if (count > len - index) count = len - index;
    memmove(buf + index, buf + index + count, len - index - count);
    len -= count;
    buf[len] = '\0';
}

String operator+(const String& a, const String& b) { String r(a); r += b; return r; }
String operator+(const String& a, const char* b) { String r(a); r += b; return r; }
String operator+(const char* a, const String& b) { String r(a); r += b; return r; }
String operator+(const String& a, char b) { String r(a); r += b; return r; }
String operator+(const String& a, int b) { String r(a); r += b; return r; }
String operator+(const String& a, unsigned int b) { String r(a); r += b; return r; }
String operator+(const String& a, long b) { String r(a); r += b; return r; }
String operator+(const String& a, unsigned long b) { String r(a); r += b; return r; }
String operator+(const String& a, double b) { String r(a); r += b; return r; }
