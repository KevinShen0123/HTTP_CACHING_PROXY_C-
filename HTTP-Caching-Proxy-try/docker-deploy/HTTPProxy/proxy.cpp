#include "Cache.hpp"  
#include <exception>
class Proxy{
private:
    const char * host;
    const char * port;
    boost::asio::io_context io_context;
    int id = 0; //request id
    std::ofstream LogStream = std::ofstream("/var/log/erss/proxy.log");
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    Cache cache;

public:
    Proxy(std::string p, int c):host(NULL), port(p.c_str()), cache(Cache(c, &lock, &LogStream)){}

    void run(){
        boost::system::error_code ec;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), strtol(port, NULL, 0)));
        while (true){
            //accept client connection
            tcp::socket * socket = new tcp::socket(io_context);
            acceptor.accept(*socket, ec);
            id++;
            if(ec.value() != 0){
                //if cannot connect, go to next connection
                // std::cerr<<"cannot connect with client: "<< ec.message()<<std::endl;
                socket->close();
                delete socket;
                continue;
            }
            std::thread t(&Proxy::requestProcess, this, socket, id-1);
            t.detach();
        }
    }
    
    /**
     * make connection from proxy to server
     * @param host server hostname
     * @param port host port
     * @return tcp socket of the connection
    */
    tcp::socket * connectToServer(const char * host, const char * port){
        boost::system::error_code ec;
        tcp::resolver resolver(io_context);

        tcp::socket * socket = new tcp::socket(io_context);
        tcp::resolver::query query(host,port);

        // Look up the domain name
        auto const results = resolver.resolve(query);
        // Make the connection on the IP address we get from a lookup
        net::connect(*socket, results);
        return socket;
};

    /**
     * process incomming request from client:
     * POST
     * GET
     * CONNECT
     * @param socket client connection
     * @param ID id number of the current client
    */
    void requestProcess(tcp::socket * socket, int ID){
        boost::system::error_code ec;
        net::ip::tcp::endpoint client_ip = socket->remote_endpoint();
        time_t now;
        time(&now);
        time_t gmt_now = mktime(gmtime(&now));
        //read request from client
        beast::flat_buffer buffer;
        http::request<http::dynamic_body> request;
        http::read(*socket, buffer, request, ec);

        //empty request, ignore
        if(ec.value() == 1){
            socket->close();
            delete socket;
            return;
        }
        //error handle: if cannot read request, or request is not valid
        //send 400 to client and close this thread
        if(ec.value() != 0 || request.find(http::field::host) == request.end()){
            //read invalid request
            pthread_mutex_lock(&lock);
            LogStream<<ID<<": Invalid Request: "<<ec.message()<<" from " << client_ip.address()\
            <<" @ "<<ctime(&gmt_now);
            pthread_mutex_unlock(&lock);
            
            // std::cerr<< "Read Request error: " << ec.value() <<", "<<ec.to_string()<< ", "<<ec.message()<<std::endl;
            http::write(*socket, make400Response(&request, ID),ec);
            if(ec.value() != 0){
                // std::cerr<< "Send 400 error: " << ec.value() <<", "<<ec.to_string()<< ", "<<ec.message()<<std::endl;
                pthread_mutex_lock(&lock);
                LogStream<<ID<<": Connection Lost"<<std::endl;
                pthread_mutex_unlock(&lock);
            }
            socket->close();
            delete socket;
            return;
        }

        pthread_mutex_lock(&lock);
        LogStream<<ID<<": \""<<request.method()<<" "<<request.target()\
        <<" "<<parseVersion(request.version())<<"\" from " <<client_ip.address()\
        <<" @ "<<ctime(&gmt_now);
        pthread_mutex_unlock(&lock);

        //connect to server
        std::string port;
        std::string host;
        isHTTPS(std::string(request.at("HOST")), &host, &port);
        tcp::socket * socket_server;
        try{
            socket_server = connectToServer(host.c_str(), port.c_str());
        }catch(std::exception & e){
            // std::cerr<< "socket error:" <<e.what()<< std::endl;
            pthread_mutex_lock(&lock);
            LogStream<<ID<<": ERROR Cannot connect to server"<<std::endl;
            pthread_mutex_unlock(&lock);
            socket->close();
            delete socket;
            return;
        }
        http::verb method = request.method();
        if (method ==http::verb::get){ //GET
            try{
                GET(&request,ID,  socket, socket_server);
            }catch(std::exception & e){
                // std::cerr<< "GET error:" <<e.what()<< std::endl;
                //if GET method throw exception, send 502 to client
                http::write(*socket, make502Response(&request, ID),ec);
                pthread_mutex_lock(&lock);
                LogStream<<ID<<": ERROR Connection Lost"<<std::endl;
                pthread_mutex_unlock(&lock);
            }
        }
        else if(method == http::verb::post){//POST
            try{
                POST(&request,ID,  socket,socket_server);
            }catch(std::exception & e){
                //if GET method throw exception, send 502 to client
                // std::cerr<< "POST error:" <<e.what()<< std::endl;
                http::write(*socket, make502Response(&request, ID),ec);
                pthread_mutex_lock(&lock);
                LogStream<<ID<<": ERROR Connection Lost"<<std::endl;
                pthread_mutex_unlock(&lock);
                //connection lost or the reponse get from server is invalid
            }

        }else if(method == http::verb::connect){//CONNECT
            try{
                CONNECT(&request,ID, socket,socket_server);
            }catch(std::exception & e){
                //if connect method throw exception, tunnel closed 
                pthread_mutex_lock(&lock);
                LogStream<<ID<<": Tunnel closed"<<std::endl;
                pthread_mutex_unlock(&lock);
                // std::cerr<< "CONNECT error:" <<e.what()<< std::endl;
            }

        }else{
            // if request method is not a valid type, should response 400
            http::write(*socket, make400Response(&request, ID),ec);
            // std::cerr<<"Bad Request Type!!!"<<std::endl;
        }
        socket->close();
        socket_server->close();
        delete socket;
        delete socket_server;

       
    }

    /**
     * see if a request is http or https, and get their host name and port
     * @param hostname host field of a request
     * @param host placeholder for the return host name
     * @param port placeholder for return the port number from hostname
     * @return true if is HTTPS else False
    */
    bool isHTTPS(std::string hostname, std::string * host, std::string * port){
        size_t f = hostname.find(":");
        if(f != std::string::npos){
            *host = hostname.substr(0, f);
            *port = hostname.substr(f+1);
            return true;
        }
        *host = hostname;
        *port = "80";
        return false;
    }

    /**
     * process post method, send request to server
     * read reponse from server and send it to client
     * @param request request get from client
     * @param socket connection to client
     * @param socket_server connection to server
    */
    void POST(http::request<http::dynamic_body> * request,int ID,  tcp::socket * socket, tcp::socket * socket_server){
        // boost::system::error_code ec;

        //send request to server
        http::write(*socket_server, *request);
        //recieve the HTTP response from the server
        boost::beast::flat_buffer buffer;
        http::response<http::dynamic_body> response;
        boost::beast::http::read(*socket_server, buffer, response);
        //send response to client
        http::write(*socket, response);
        pthread_mutex_lock(&lock);
         LogStream<<ID<<": Responding \"" \
        << parseVersion(response.version())<< " " << response.result_int() << " "<<response.reason()<<"\""<<std::endl;
        pthread_mutex_unlock(&lock);
    }

    /**
     * process CONNECT method, build a tunnel between client and server
     * send data in buffer between them
     * @param request request get from client
     * @param socket connection to client
     * @param socket_server connection to server
    */
    void CONNECT(http::request<http::dynamic_body> * request,int ID, tcp::socket * socket, tcp::socket * socket_server){
        //send success to client, build the tunnel
        int status;
        std::string message = "HTTP/1.1 200 OK\r\n\r\n";
         pthread_mutex_lock(&lock);
         LogStream<<ID<<": Responding \"HTTP/1.1 200 OK\""<<std::endl;
        pthread_mutex_unlock(&lock);
        status = net::write(*socket, net::buffer(message));
        while(true){
            //byte of data available to read from server
            int server_byte = socket_server->available();
            //byte of data available to read from client
            int clinet_byte = socket->available();

            //send message between client and server
            if(server_byte > 0){
                std::vector<char> bu1(server_byte);
                net::read(*socket_server, net::buffer(bu1));
                net::write(*socket, net::buffer(bu1));
            }
            if(clinet_byte > 0){
                std::vector<char> bu2(clinet_byte);
                net::read(*socket, net::buffer(bu2));
                net::write(*socket_server, net::buffer(bu2));
            }
            //if connection is closed, break the loop
            if(!socket_server->is_open()||!socket->is_open()){
                pthread_mutex_lock(&lock);
                LogStream<<ID<<": Tunnel closed"<<std::endl;
                pthread_mutex_unlock(&lock);
                break;
            }
        }
    }

    /**
     * process GET method, if cached...
     * read reponse from server and send it to client
     * @param request request get from client
     * @param socket connection to client
     * @param socket_server connection to server
    */
    void GET(http::request<http::dynamic_body> * request,int ID, tcp::socket * socket, tcp::socket * socket_server){
        // POST(request, socket, socket_server);
        // boost::system::error_code ec;

        std::string key;
        key = std::string((*request)[http::field::host]) +": "+ std::string(request->target());
        
        if(cache.isInCache(key)){
            //get response from cache
            http::response<http::dynamic_body> * response = cache.get(key);
            
            if(needValidationWhenAccess(response, ID)){
                // pthread_mutex_lock(&lock);
                // LogStream<<ID << ": in cache, requires validation"<<std::endl;
                // pthread_mutex_unlock(&lock);
                http::response<http::dynamic_body> vali_response = doValidation(socket_server,request, response, ID);
                if(vali_response.result_int() ==200){
                    cache.update(key, vali_response);
                    http::write(*socket, vali_response);
                    pthread_mutex_lock(&lock);
                    LogStream<<ID<<": Responding \"" \
                    << parseVersion(vali_response.version())<< " " << vali_response.result_int() << " "<<vali_response.reason()<<"\""<<std::endl;
                    pthread_mutex_unlock(&lock);
                }else{
                    http::write(*socket, *response);
                    pthread_mutex_lock(&lock);
                    LogStream<<ID<<": Responding \"" \
                    << parseVersion(response->version())<< " " << response->result_int() << " "<<response->reason()<<"\""<<std::endl;
                    pthread_mutex_unlock(&lock);

                }
            }else{
                pthread_mutex_lock(&lock);
                LogStream<<ID<< ": in cache, valid"<<std::endl;
                // Send response to the client
                LogStream<<ID<<": Responding \"" \
                << parseVersion(response->version())<< " " << response->result_int() << " "<<response->reason()<<"\""<<std::endl;
                pthread_mutex_unlock(&lock);
                http::write(*socket, *response);
            }
            // std::cout<<"Cached response is: "<<response->base()<<std::endl;
        }else{
            pthread_mutex_lock(&lock);
            LogStream<<ID<<": not in cache"<<std::endl;
            LogStream<<ID<<": Requesting \""<<request->method()<<" "<<request->target()\
            <<" "<<parseVersion(request->version())<<"\" from " << request->at("host")<<std::endl;
            pthread_mutex_unlock(&lock);
            //connect to server
            http::write(*socket_server, *request);

            //recieve the HTTP response from the server
            boost::beast::flat_buffer buffer;
            http::response<http::dynamic_body> response;
            boost::beast::http::read(*socket_server, buffer, response);
            pthread_mutex_lock(&lock);
            LogStream<<ID<<": Received \"" \
            << parseVersion(response.version())<< " " << response.result_int() <<" "<< response.reason() \
            <<"\" from "<< request->at("host")<<std::endl;
            pthread_mutex_unlock(&lock);
            time_t expire;
            //store in cache
            if(cacheCanStore(request, &response, ID)){
                cache.put(key, response);
                if(hasValidation(&response)){
                    pthread_mutex_lock(&lock);
                    LogStream<<ID<< ": cached, but requires re-validation"<<std::endl;
                    pthread_mutex_unlock(&lock);
                }
                else if(getExpireTime(&response, &expire)==1){
                    pthread_mutex_lock(&lock);
                    LogStream<<ID<< ": cached, expires at "<<ctime(&expire);
                    pthread_mutex_unlock(&lock);
                }
                else{
                    pthread_mutex_lock(&lock);
                    LogStream<<ID<< ": cached, does not expire "<<std::endl;
                    pthread_mutex_unlock(&lock);
                }
            }
            // Send the response to the client
            http::write(*socket, response);
            pthread_mutex_lock(&lock);
            LogStream<<ID<<": Responding \"" \
            << parseVersion(response.version())<< " " << response.result_int() << " "<<response.reason()<<"\""<<std::endl;
            pthread_mutex_unlock(&lock);
            // std::cout<<"response is: "<<response.base()<<std::endl;
        }
        
    }

    /**
     * check if the response get from a cache need to be validate
     * yes: 1. the response has "no-cache", "must-revalidate" in cache-control
     *      2. the response is not fresh
     * otherise, no
     * @param response the response stored in cache
     * @return true is need validate; false if not
    */
    bool needValidationWhenAccess(http::response<http::dynamic_body> * response, int ID){
        if(hasValidation(response)){
            pthread_mutex_lock(&lock);
            LogStream<<ID << ": in cache, requires validation"<<std::endl;
            pthread_mutex_unlock(&lock);
            return true;
        }else{
            if(isFresh(response)){
                return false;
            }else{
                time_t expire;
                if(getExpireTime(response, &expire)==1){
                    pthread_mutex_lock(&lock);
                    LogStream<<ID<< ": in cache, but expired at "<<ctime(&expire);
                    pthread_mutex_unlock(&lock);
                }

                return true;
            }
        }
    
    }

    /**
     * Get the exprire time of the response
     * @param response the reponse get from the server or from cache 
     * @param expire the placeholder for the expire time to return
     * @return 1 if expires at some time, 0 if not 
    */
    int getExpireTime(http::response<http::dynamic_body> * response, time_t * expire){
        //has cache control
        try{
            if(response->find(http::field::cache_control) != response->end()){
                std::string str((*response)[http::field::cache_control]);
                std::map<std::string, long> fields = parseFields(str);

                std::string date_str((*response)[http::field::date]);
                time_t date_value = parseDatetime(date_str);

                if(fields.find("max-age") != fields.end()){
                    *expire = date_value + fields["max-age"];
                    return 1;
                }else{
                    //never expire
                    expire = NULL;
                    return 0;
                }
            }else{
                if(response->find(http::field::expires)== response->end()){
                    //never expire
                    expire = NULL;
                    return 0; 
                }else{
                    std::string expire_str((*response)[http::field::expires]);
                    //what is the str is just 0 or -1;
                    *expire = parseDatetime(expire_str);
                    return 1;
                }
            }
        }catch(std::invalid_argument & e){
            return 0;
            expire = NULL;
        }
        return 0;
    }

    bool hasValidation(http::response<http::dynamic_body> * response){
        if(response->find(http::field::cache_control) != response->end()){
            std::string str((*response)[http::field::cache_control]);
            std::map<std::string, long> fields = parseFields(str);
            
            //if no-cache or must-revalidate, need validation
            if(fields.find("no-cache") != fields.end() || fields.find("must-revalidate") != fields.end()){
                return true;
            }
            //if max-age == 0
            if(fields.find("max-age") != fields.end()){
                if(fields["max-age"] == 0){
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * Based on the new request get from client and old response saved in cache,
     * add if_none_match and if_modified_since fields to generate the new conditonal request
     * @param request new request get from client
     * @param reponse old response saved in cache
     * @return conditonal request
    */
    http::request<http::dynamic_body> makeConditionalRequest(http::request<http::dynamic_body> * request, http::response<http::dynamic_body> * response){
        http::request<http::dynamic_body> new_request = *request;

        if(response->find(http::field::etag)!=response->end()){
            new_request.set(http::field::if_none_match,(*response)[http::field::etag]);
        }
        
        if(response->find(http::field::last_modified)!=response->end()){
             new_request.set(http::field::if_modified_since, (*response)[http::field::last_modified]);
        }
        return new_request;
    }

     /**
     * do one validation. send conditional request to server
     * @param socket the socket connection to server
     * @param request the request send from client
     * @param response response saved in cache
     * @return the response got from the server, 200 if updated, 304 if not
    */
    http::response<http::dynamic_body> doValidation(tcp::socket * socket_server, http::request<http::dynamic_body> * request, http::response<http::dynamic_body> * response, int ID){
        // boost::system::error_code ec;
        http::request<http::dynamic_body> Crequest = makeConditionalRequest(request, response);
        http::write(*socket_server, Crequest);
        pthread_mutex_lock(&lock);
        LogStream<<ID<<": Validating \""<<Crequest.method()<<" "<<Crequest.target()\
        <<" "<<parseVersion(Crequest.version())<<"\" from "<<Crequest.at("host")<<std::endl;
        pthread_mutex_unlock(&lock);
        boost::beast::flat_buffer buffer;
        http::response<http::dynamic_body> new_response;
        
        boost::beast::http::read(*socket_server, buffer, new_response);
        pthread_mutex_lock(&lock);
        LogStream<<ID<<": Recieve validation \""<< parseVersion(new_response.version())\
        << " " << new_response.result_int() <<" "<< new_response.reason() \
        <<"\" from "<< Crequest.at("host")<<std::endl;
        pthread_mutex_unlock(&lock);
        return new_response;
    }

    /**
     * indicate whether the reponse can be stored in the cache
     * @param request
     * @param response
     * @return yes if can cache; no if not
    */
    bool cacheCanStore(http::request<http::dynamic_body> * request, http::response<http::dynamic_body> * response, int ID){
        //response is not cacheable 200 ok
        if(response->result_int() != 200){
            pthread_mutex_lock(&lock);
            LogStream<<ID <<": not cacheable because \"Response code is "<<response->result_int()<<"\""<<std::endl;
            pthread_mutex_unlock(&lock);
            return false;
        }
        //no cache control field
        if(response->find(http::field::cache_control) == response->end()){
            return true;
        }else{
            std::string str((*response)[http::field::cache_control]);
            std::map<std::string, long> fields = parseFields(str);
            //the "no-store" cache directive does not appear in request or response header fields
            if(fields.find("no-store") != fields.end()){
                pthread_mutex_lock(&lock);
                LogStream<<ID <<": not cacheable because \"no-store\""<<std::endl;
                pthread_mutex_unlock(&lock);
                return false;
            }else{
                //the "private" response directive (see Section 5.2.2.6) does not
                //appear in the response, if the cache is shared, and
                if(fields.find("private") != fields.end()){
                    pthread_mutex_lock(&lock);
                    LogStream<<ID <<": not cacheable because \"private\""<<std::endl;
                    pthread_mutex_unlock(&lock);
                    return false;
                }else{
                    return true;
                }
            }
        }
        
        return false;
    }

    /**
     * check whether the reponse in cache is still fresh
     * @param response the response stored in cache
     * @return true if still fresh; false if not
    */
    bool isFresh(http::response<http::dynamic_body> * response){
        time_t now;
        time(&now);
        time_t gmt_now = mktime(gmtime(&now));

        time_t expire;
        int status = getExpireTime(response, &expire);
        if(status == 0){
            return true;
        }else{
            if (expire > gmt_now){
                return true;
            }else{
                return false;
            }
        }
    }
   

    http::response<http::dynamic_body> make400Response(http::request<http::dynamic_body> * request, int ID ){
        http::response<http::dynamic_body> response;
        response.result(boost::beast::http::status::bad_request);
        response.version(request->version());
        response.prepare_payload();
        pthread_mutex_lock(&lock);
        LogStream<<ID<<": Responding \"" \
        << parseVersion(response.version())<< " " << response.result_int() << " "<<response.reason()<<"\""<<std::endl;
        pthread_mutex_unlock(&lock);
        return response;
    }

    http::response<http::dynamic_body> make502Response(http::request<http::dynamic_body> * request, int ID ){
        http::response<http::dynamic_body> response;
        response.result(http::status::bad_gateway);
        response.version(11);
        response.prepare_payload();
        pthread_mutex_lock(&lock);
        LogStream<<ID<<": Responding \"" \
        << parseVersion(response.version())<< " " << response.result_int() << " "<< response.reason()<<"\""<<std::endl;
        pthread_mutex_unlock(&lock);
        return response;
    }
};

int main(){
    int status = daemon(1,1);
    if(status == -1){
        std::cerr<<"Daemon fail"<<std::endl;
        return EXIT_FAILURE;
    }
    std::string host = "12345";
    Proxy p(host, 1000);
    p.run();
    return EXIT_SUCCESS;
}
