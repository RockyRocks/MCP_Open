#include <iostream>

#if USE_UWS
extern void run_uws_server(int port);
#else
extern void run_httplib_server(int port);
#endif

int main(int /*argc*/, char** /*argv*/){
    int port = 9001;
#if USE_UWS
    std::cout << "Starting uWebSockets server..." << std::endl;
    run_uws_server(port);
#else
    std::cout << "Starting cpp-httplib server..." << std::endl;
    run_httplib_server(port);
#endif
    return 0;
}
