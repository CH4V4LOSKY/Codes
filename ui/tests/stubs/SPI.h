#pragma once
#include <string>
#include <deque>
#include <cctype>
#include <algorithm>
class String {
public:
  std::string value;
  String(const char *s=""):value(s){}
  void trim(){const auto a=value.find_first_not_of(" \t\r\n");if(a==std::string::npos){value.clear();return;}value=value.substr(a,value.find_last_not_of(" \t\r\n")-a+1);}
  void toUpperCase(){for(char &c:value)c=char(std::toupper(static_cast<unsigned char>(c)));}
  size_t length()const{return value.size();}
  String& operator+=(char c){value+=c;return *this;}
  bool operator==(const char *s)const{return value==s;}
};
struct SerialStub {
  std::deque<char> input;std::string output;
  void begin(int){}
  int available(){return int(input.size());}
  int read(){char c=input.front();input.pop_front();return c;}
  void print(const char *s){output+=s;}
  void print(const String&s){output+=s.value;}
  void println(const char*s){print(s);output+='\n';}
  void println(const String&s){print(s);output+='\n';}
};
inline SerialStub Serial;
struct SPIStub {void begin(int=0,int=0,int=0,int=0){}};
inline SPIStub SPI;
inline void delay(int){}
