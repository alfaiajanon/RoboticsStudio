/*
 * WString.h: a minimal Arduino String (heap buffer). Covers the common
 * subset: building text from numbers, concatenation, comparison, search,
 * substrings and conversions.
 */
#ifndef WSTRING_H
#define WSTRING_H

#include <stddef.h>

class __FlashStringHelper;

class String {
    char* buf = nullptr;
    unsigned int len = 0;

    void assign(const char* s, unsigned int n);
    void append(const char* s, unsigned int n);

    public:
        String(const char* s = "");
        String(const __FlashStringHelper* s) : String(reinterpret_cast<const char*>(s)) {}
        String(const String& other);
        String(String&& other) noexcept;
        explicit String(char c);
        explicit String(int value, unsigned char base = 10);
        explicit String(unsigned int value, unsigned char base = 10);
        explicit String(long value, unsigned char base = 10);
        explicit String(unsigned long value, unsigned char base = 10);
        explicit String(float value, unsigned char decimals = 2);
        explicit String(double value, unsigned char decimals = 2);
        ~String();

        String& operator=(const String& other);
        String& operator=(String&& other) noexcept;
        String& operator=(const char* s);

        String& operator+=(const String& s) { append(s.buf, s.len); return *this; }
        String& operator+=(const char* s);
        String& operator+=(char c);
        String& operator+=(int v) { return *this += String(v); }
        String& operator+=(unsigned int v) { return *this += String(v); }
        String& operator+=(long v) { return *this += String(v); }
        String& operator+=(unsigned long v) { return *this += String(v); }
        String& operator+=(float v) { return *this += String(v); }
        String& operator+=(double v) { return *this += String(v); }
        bool concat(const String& s) { *this += s; return true; }

        unsigned int length() const { return len; }
        const char* c_str() const { return buf ? buf : ""; }
        char charAt(unsigned int i) const { return i < len ? buf[i] : 0; }
        char operator[](unsigned int i) const { return charAt(i); }
        char& operator[](unsigned int i);
        void setCharAt(unsigned int i, char c) { if (i < len) buf[i] = c; }

        bool equals(const String& s) const;
        bool equals(const char* s) const;
        bool equalsIgnoreCase(const String& s) const;
        bool operator==(const String& s) const { return equals(s); }
        bool operator==(const char* s) const { return equals(s); }
        bool operator!=(const String& s) const { return !equals(s); }
        bool operator!=(const char* s) const { return !equals(s); }
        int compareTo(const String& s) const;
        bool startsWith(const String& s) const;
        bool endsWith(const String& s) const;

        int indexOf(char c, unsigned int from = 0) const;
        int indexOf(const String& s, unsigned int from = 0) const;
        int lastIndexOf(char c) const;
        String substring(unsigned int from) const { return substring(from, len); }
        String substring(unsigned int from, unsigned int to) const;

        long toInt() const;
        float toFloat() const;
        double toDouble() const;

        void trim();
        void toUpperCase();
        void toLowerCase();
        void replace(char find, char with);
        void remove(unsigned int index, unsigned int count = (unsigned int)-1);
};

String operator+(const String& a, const String& b);
String operator+(const String& a, const char* b);
String operator+(const char* a, const String& b);
String operator+(const String& a, char b);
String operator+(const String& a, int b);
String operator+(const String& a, unsigned int b);
String operator+(const String& a, long b);
String operator+(const String& a, unsigned long b);
String operator+(const String& a, double b);

#endif   // WSTRING_H
