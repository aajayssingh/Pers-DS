// multimap::equal_range
#include <iostream>
#include <map>

typedef intptr_t ajay_t;
int main ()
{
  std::multimap<char,int> mymm;
ajay_t aj = 0;
  mymm.insert(std::pair<char,int>('a',10));
  mymm.insert(std::pair<char,int>('b',20));
  mymm.insert(std::pair<char,int>('b',30));
  mymm.insert(std::pair<char,int>('b',40));
  mymm.insert(std::pair<char,int>('b',50));

  std::cout << "mymm contains:\n";
//  for (char ch='a'; ch<='d'; ch++)
//  {
    char ch = 'b';
    char* chp = "z";

    if(*chp == 'z')
        std::cout<<"yes eq";
    std::pair <std::multimap<char,int>::iterator, std::multimap<char,int>::iterator> ret;
    ret = mymm.equal_range(ch);

    std::cout << ch << " =>";
    for (std::multimap<char,int>::iterator it=ret.first; it!=ret.second; ++it)
      std::cout << ' ' << it->second;
      std::cout<<"lehh"<<std::endl;
    std::multimap<char,int>::iterator it = ret.first;
    //it++;
    std::cout << it->second<<'\n';
//  }

  return 0;
}
