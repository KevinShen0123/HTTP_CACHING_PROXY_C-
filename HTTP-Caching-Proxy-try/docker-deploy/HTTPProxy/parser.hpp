#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio.hpp>
#include <boost/config.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time.hpp>
#include <ctime>          // std::tm
#include <exception>

#include <sstream>
#include <locale>

namespace beast = boost::beast;         
namespace http = beast::http;           
namespace net = boost::asio;            
using tcp = boost::asio::ip::tcp;      
namespace pt = boost::posix_time;
namespace dt = boost::date_time;

time_t parseDatetime(std::string date_str){
    std::string format_str = "%a, %d %b %Y %H:%M:%S GMT";

    tm tm;
    tm.tm_isdst = 0;
    if (strptime(date_str.c_str(), format_str.c_str(), &tm) == NULL) {
        std::cerr << "Failed to parse HTTP-date string" << std::endl;
        throw std::invalid_argument("Unable to parse datetime");
    }
    time_t time = mktime(&tm);

    // Print the parsed time
    // std::cout<<"String is: "<<date_str<<std::endl;
    // std::cout << "Parsed time: " << std::ctime(&time);
    return time;
}

std::vector<std::string> split(std::string str_, char delimiter){
    std::string str = "";
    for(int i = 0; i < str_.size(); i++){
        if(str_[i]!=' '){
            str+=str_[i];
        }
    }

    std::vector<std::string> result;
    size_t start = 0, end = 0;
    while ((end = str.find(delimiter, start)) != std::string::npos) {
        result.push_back(str.substr(start, end - start));
        start = end + 1;
    }
    result.push_back(str.substr(start));
    return result;
}

std::map<std::string, long> parseFields(std::string & str){
    std::map<std::string, long> result;
    std::vector<std::string> fields = split(str, ',');
    int end;
    for(int i  = 0; i < fields.size(); i++){
        end = 0;
        if((end= fields[i].find('=', 0)) != std::string::npos){
            result[fields[i].substr(0, end)] = std::stol(fields[i].substr(end+1),NULL,10);
        }else{
            result[fields[i]] = -1;
        }
    }
    return result;
}

std::string parseVersion(unsigned version){
    unsigned major = version / 10;
    unsigned minor = version % 10;
    std::string ans = "HTTP/";
    ans = ans+ std::to_string(major) + "."  +std::to_string(minor); 
    return ans;
}