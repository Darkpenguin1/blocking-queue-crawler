#include <iostream>
#include <string>
#include <queue>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>
#include <curl/curl.h>
#include <stdexcept>
#include "rapidjson/error/error.h"
#include "rapidjson/reader.h"
#include "blocking_queue.hpp"
#include <atomic>
#include <thread>
#include <mutex>

struct ParseException : std::runtime_error, rapidjson::ParseResult {
    ParseException(rapidjson::ParseErrorCode code, const char* msg, size_t offset) : 
        std::runtime_error(msg), 
        rapidjson::ParseResult(code, offset) {}
};

#define RAPIDJSON_PARSE_ERROR_NORETURN(code, offset) \
    throw ParseException(code, #code, offset)

#include <rapidjson/document.h>
#include <chrono>

using namespace std;
using namespace rapidjson;

bool debug = false;


// Updated service URL
const string SERVICE_URL = "http://hollywood-graph-crawler.bridgesuncc.org/neighbors/";


// Function to HTTP ecnode parts of URLs. for instance, replace spaces with '%20' for URLs
string url_encode(CURL* curl, string input) {
  char* out = curl_easy_escape(curl, input.c_str(), input.size());
  string s = out;
  curl_free(out);
  return s;
}

// Callback function for writing response data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

// Function to fetch neighbors using libcurl with debugging
string fetch_neighbors(CURL* curl, const string& node) {

    string url = SERVICE_URL + url_encode(curl, node);
    string response;

    if (debug)
      cout << "Sending request to: " << url << endl;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // Verbose Logging

    // Set a User-Agent header to avoid potential blocking by the server
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: C++-Client/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        cerr << "CURL error: " << curl_easy_strerror(res) << endl;
    } else {
      if (debug)
        cout << "CURL request successful!" << endl;
    }

    // Cleanup
    curl_slist_free_all(headers);

    if (debug) 
      cout << "Response received: " << response << endl;  // Debug log

    return (res == CURLE_OK) ? response : "{}";
}

// Function to parse JSON and extract neighbors
vector<string> get_neighbors(const string& json_str) {
    vector<string> neighbors;
    try {
      Document doc;
      doc.Parse(json_str.c_str());
      
      if (doc.HasMember("neighbors") && doc["neighbors"].IsArray()) {
        for (const auto& neighbor : doc["neighbors"].GetArray())
	  neighbors.push_back(neighbor.GetString());
      }
    } catch (const ParseException& e) {
      std::cerr<<"Error while parsing JSON: "<<json_str<<std::endl;
      throw e;
    }
    return neighbors;
}

/*
void worker(const std::vector<std::string> &frontier, size_t begin, size_t end, 
  std::unordered_set<std::string> &visited, std::vector<std::string> &nextFrontier, std::mutex &mu
) 
*/

void worker(blocking_queue<pair<string, int>> &q, unordered_set<string> &visited, int depth, std::mutex &visitedMutex, std::atomic<int>& pending,
    std::vector<std::string> &result, std::mutex &resultMutex) {
    CURL *curl = curl_easy_init();
    if (!curl) return;
    std::pair<std::string, int> item;

    while (q.pop(item)){
        const std::string& node = item.first;
        int currLevel = item.second;
        {
            lock_guard<mutex> lock(resultMutex);
            result.push_back(node);
        }
        if (currLevel < depth){
            try {
                for (const auto& neighbor : get_neighbors(fetch_neighbors(curl, node))) {
                    bool isNew = false; 

                    {   // check if node is new within the set
                        std::lock_guard<std::mutex> lock(visitedMutex);
                        auto [it, inserted] = visited.insert(neighbor);
                        isNew = inserted;
                    }
                    if (isNew){
                        pending.fetch_add(1, std::memory_order_relaxed);
                        q.push({neighbor, currLevel + 1});
                    }
                }
            }
            catch(const std::exception& e) {
                std::cerr << e.what() << '\n';
            }
            
        }
        if (pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            q.all_done(); // pending just became 0 => wake everyone, stop
        }
    }
    curl_easy_cleanup(curl);

}


// BFS Traversal Function
vector<string> bfs(const string& start, int depth) {
    blocking_queue<pair<string, int>> q;
    unordered_set<string> visited;
    vector<string> result;
    std::mutex visitedMutex;
    std::mutex resultMutex;
    std::atomic<int> pending{0};
    pending = 1; 
    
    q.push({start, 0});
    visited.insert(start);

    const int NUM_THREADS = 8;

    vector<thread> threads;
    threads.reserve(NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(
            worker,
            std::ref(q),
            std::ref(visited),
            depth,
            std::ref(visitedMutex),
            std::ref(pending),
            std::ref(result),
            std::ref(resultMutex)
        );
    }

    // Wait for all workers to finish
    for (auto& t : threads)
        t.join();

    return result;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <node_name> <depth>\n";
        return 1;
    }

    string start_node = argv[1];     // example "Tom%20Hanks"
    int depth;
    try {
        depth = stoi(argv[2]);
    } catch (const exception& e) {
        cerr << "Error: Depth must be an integer.\n";
        return 1;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);


    const auto start{std::chrono::steady_clock::now()};
    
    
    for (const auto& node : bfs(start_node, depth))
        cout << "- " << node << "\n";

    const auto finish{std::chrono::steady_clock::now()};
    const std::chrono::duration<double> elapsed_seconds{finish - start};
    std::cout << "Time to crawl: "<<elapsed_seconds.count() << "s\n";
    curl_global_cleanup();
    
    return 0;
}
