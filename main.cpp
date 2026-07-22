#include<iostream>
#include "crawler.h"
using namespace std;
int main(){
    mongocxx::instance instance{};
    crawler cr("https://www.youtube.com/watch?v=g9eHtAssgg0&list=RDZ1cHKm-Bww4&index=3");
    cr.start();
}