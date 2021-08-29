/*
    Debian Web Server
    Created by: Nathaniel Morrow
    2/26/2021
*/

#include <iostream>
#include <cstring>
#include <thread>
#include <fstream>

#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

void connection_handler(int socket);
std::string chomp(std::string &food);
int get_file_size(std::ifstream &file);
int run_lua_script(int socket, std::string request);
static int print(lua_State *L);

int main(int argv, char** argc){
    int port_num = 0;
    if(argv < 2) { 
        std::cout << "Input a port: ";
        std::cin >> port_num;  //short, network byte order
    } else port_num = std::stoi(argc[1]);

    //  1) Create socket s - listener socket
    int server_socket, client_socket, clilen;
    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    //specify an adress for the socket
    struct sockaddr_in server_addr, client_addr;

   //  2) Set the socket options - fill sock_aaddr_in with local addressinfo
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    //  3) Bind the socket s to a port (given on the command line)
    bind(server_socket, (struct sockaddr*) &server_addr, sizeof(server_addr));
    //int connection_status = ~~ if (connection_status == -1)
        //std::cout << "Connection error!\n";

    //  4) Call the listen() function to have the OS queue connection requests
    listen(server_socket, 50);
    
    //  5)  Call accept() to accept a new connection -wait for client and accept
          /* This will block. When a client connects, accept() will unblock
             and return a data socket. */
    while(1) {
        clilen =  sizeof(client_addr);
        client_socket = accept(server_socket, (struct sockaddr *) &client_addr, (unsigned int*) &clilen);
        if(client_socket < 0) {std::cout << "connection error!";}

        //  6) Create a thread that calls the Connection_handler function, passing
        //  it the data socket
        std::thread connection(connection_handler, client_socket);
        connection.detach();
    }
    return 0;
}

/*
    Name: connection_handler
    Function: Accepts a socket and manages a connection following HTTP.
    *Thread Safe* 
*/
void connection_handler(int socket){
//  1) Read a single HTTP request from the data socket.
    char buff[1024];
    buff[0] = 0;
    bool keep_alive = false;
    do{
        FILE *fp = fdopen(socket, "r+b");
        
    //  2) Parse the HTTP request line to discover necessary information
    //     needed to retrieve the appropriate document
    //     //get resource path, http version. Only supporting GET requests here.
    //      If http 1.1 keep_alive = true!
        fgets(buff, 1024, fp);
        //std::cout << buff << std::endl;
        std::string key     = "",
                    val     = buff,
                    method  = chomp(val),
                    path    = chomp(val),
                    version = chomp(val);
        bool   headers_done = false;

    //  3) for each header line...
    //     a) parse the header line to retrieve the key-value pair
    //     b) if the key is keep-alive, then
    //          remember that the connection is persistent
        while(!headers_done){
            fgets(buff, 1024, fp);
            if (buff[0] == '\r') headers_done = true; 
            else{ //parse key/value pairs
                val = buff;
                key = chomp(val);
                val = chomp(val);
                //record if connection keep alive header is present
                if(!keep_alive) keep_alive = (key =="Connection:" && val == "Keep-Alive") || version == "HTTP/1.1";
            }
        }

        //if lua, run the script instead
        if((path.substr(path.find('.'), 4) == ".lua")){
            run_lua_script(socket, path);
        } else {

    //  4) if the request can be handled
    //     /* the document exists at the given path */
            std::ifstream file;
            char cwd[1024];
            getcwd(cwd, 1024);
            std::string directory = cwd;
            std::string response = "";
            file.open(directory + path);
            if(file.good()){
        //      a) Determine the size of the file on disk
                int file_size = get_file_size(file);
        //      b) Construct a reply
        //          file sizes.)
                response = "HTTP/1.0 200 OK\r\nContent-Length: " + std::to_string(file_size);
                response.append("\n\r\n");
                //send headers
                 send(socket, response.c_str(), strlen(response.c_str()), 0);
                //fetch file
                char buf[4096] = {0};
                file.read(buf, 4096);
                int count = file.gcount();
                while (count > 0) {
                    send(socket, buf, 4096, 0);
                    file.read(buf, 4096);
                    count = file.gcount();
                }
            } else {
    //     Otherwise the web server will return the appropriate error response to
    //     the client
                response =  "HTTP/1.1 404 File Not Found\r\n\r\n"
                            "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">"
                            "<html><head>"
                            "<title>404 Not Found</title>"
                            "</head><body>"
                            "<h1>Not Found</h1>"
                            "<p>The requested URL was not found on this server.</p>";;
                
                send(socket, response.c_str(), strlen(response.c_str()), 0);
            }
            //std::cout << std::endl <<std::endl;
        }

    //   6) if the client is using HTTP 1.0 and the connection is not persistent
    //          Close the client connection otherwise
    //        /*the client is using HTTP 1.0 with a “Connection: Keep-Alive”
    //          header or is using HTTP 1.1 */
    //          loop to step 1
    fflush(fp);
    }while(keep_alive);
    close(socket);
    return;
}

/*
    Name: chomp
    Function: Accepts a string, removes the first portion up to the first space or endline and returns it.
*/
std::string chomp(std::string& food){
    std::string bite = "";
    if(food.find(' ') < food.find('\n'))
        bite = food.substr(0, food.find(' '));   //grab until ' '
    else bite = food.substr(0, food.find('\n')); // or \n, whichever is closest

    food = food.substr(food.find(' ') + 1, food.size()); //cut off bitten 'food'
    return bite;
}

/*
    Name: get_file_size
    Function: Takes a file and returns its size in bytes.
*/
int get_file_size(std::ifstream &file){
    int beg = file.tellg();
    file.seekg(0, std::ios::end);
    int length = file.tellg();
    file.seekg(beg, std::ios::beg);
    return length;
}

/*
    Name: run_lua_script
    Function: takes a socket and a request in order to generate/send dynamic content.
*/
int run_lua_script(int socket, std::string request){
    //parse request
    std::string response = "";
    int ret, result;
    //double sum;
    request = request.substr(1,request.length()-1);
    std::string file_name = request.substr(0, request.find('?'));
    std::string key = "";
    std::string val = "";

    lua_State *L = luaL_newstate();
     
    luaL_openlibs(L); // Load Lua libraries 

    ret = luaL_loadfile(L, file_name.c_str());
    if (ret) { //file is bad, 404
        std::cerr << "Couldn't load file: " << lua_tostring(L, -1) << std::endl;
        response =  "HTTP/1.1 404 File Not Found\r\n\r\n"
                            "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">"
                            "<html><head>"
                            "<title>404 Not Found</title>"
                            "</head><body>"
                            "<h1>Not Found</h1>"
                            "<p>The requested URL was not found on this server.</p>";;
                
        send(socket, response.c_str(), strlen(response.c_str()), 0);
        return 1;
    }
      
    //file is good, send header data
    response = "HTTP/1.0 200 OK\r\n\r\n";
    send(socket, response.c_str(), strlen(response.c_str()), 0);

    //time to parse the rest or the request
    request = request.substr(request.find('?') + 1, request.length());
    //std::cout << request << std::endl;

    //! Note, this only supports the two requests in the example page, this must be modified to accomodate for more elaborate requests
    lua_newtable(L);   
    key = request.substr(0, request.find('='));
    val = request.substr(request.find('=')+1, request.find('&') - request.find('=')-1);
    request = request.substr(request.find('&')+1, request.length());
    lua_pushstring(L,  key.c_str());
    lua_pushstring(L, val.c_str());
    lua_rawset(L, -3);      // Stores the pair in the table 
    key = request.substr(0, request.find('='));
    val = request.substr(request.find('=')+1, request.find('&') - request.find('=')-1);
    request = request.substr(request.find('&')+1, request.length());
    lua_pushstring(L, key.c_str());
    lua_pushstring(L, val.c_str());
    lua_rawset(L, -3);
      
    lua_setglobal(L, "REQUEST");  //table name
     
    int *ud = (int *) lua_newuserdata(L, sizeof(int));
    *ud = socket; // file descriptor 1 is standard out, this means I can pass the socket

    lua_pushcclosure(L, print, 1);
    lua_setglobal(L, "print");                          
    result = lua_pcall(L, 0, 1, 0);
    if (result) {
      std::cerr << "Failed to run script: " << lua_tostring(L, -1) << std::endl;
      exit(1);
    }

    lua_pop(L, 1);  /* Take the returned value out of the stack */
    lua_close(L);   /* Adios, Lua */

    return 0;
}
//renamed to replace standard lua print function
static int print(lua_State *L) {
  /* just to show that I can get the upvalue.  This is what makes this function
     a closure. */
  int *fd = (int *) lua_touserdata(L, lua_upvalueindex(1));
  // Now get the single argument passed by the call in the Lua script.
  const char *msg = (char *) lua_tostring(L, -1);
  // Now, write the argument using the file descriptor upvalue.
  write(*fd, msg, strlen(msg));
  write(*fd, "\n", 1);
  return 0;	
}
