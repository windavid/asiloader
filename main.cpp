#include "server_http.hpp"

#include <boost/filesystem.hpp>
#include <fstream>
#include <vector>

using namespace std;

typedef SimpleWeb::Server<SimpleWeb::HTTP> HttpServer;

int main(int argc, char* argv[]) {
	if (argc != 2) {
		std::cout << "Usage: " << argv[0] << " port" << std::endl;
		return 1;
	}
	HttpServer server;
	int port = stoi(argv[1]);
	server.config.port = 8080;

	server.default_resource["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
		try {
			auto web_root_path = boost::filesystem::current_path(); // canonical("web");
			auto path = boost::filesystem::canonical(web_root_path / request->path);
			// Check if path is within web_root_path
			if (distance(web_root_path.begin(), web_root_path.end()) > distance(path.begin(), path.end()) ||
				!equal(web_root_path.begin(), web_root_path.end(), path.begin()))
				throw invalid_argument("path must be within root path");
			if (boost::filesystem::is_directory(path))
				throw invalid_argument("can't send directory");

			SimpleWeb::CaseInsensitiveMultimap header;

			auto ifs = make_shared<ifstream>();
			ifs->open(path.string(), ifstream::in | ios::binary | ios::ate);

			if (*ifs) {
				auto length = ifs->tellg();
				ifs->seekg(0, ios::beg);

				header.emplace("Content-Length", to_string(length));
				response->write(header);

				// Trick to define a recursive function within this scope (for your convenience)
				class FileServer {
				public:
					static void read_and_send(const shared_ptr<HttpServer::Response> &response, const shared_ptr<ifstream> &ifs) {
						// Read and send 128 KB at a time
						static vector<char> buffer(131072); // Safe when server is running on one thread
						streamsize read_length;
						if ((read_length = ifs->read(&buffer[0], buffer.size()).gcount()) > 0) {
							response->write(&buffer[0], read_length);
							if (read_length == static_cast<streamsize>(buffer.size())) {
								response->send([response, ifs](const SimpleWeb::error_code &ec) {
									if (!ec)
										read_and_send(response, ifs);
									else
										cerr << "Connection interrupted" << endl;
								});
							}
						}
					}
				};
				FileServer::read_and_send(response, ifs);
			}
			else
				throw invalid_argument("could not read file");
		}
		catch (const exception &e) {
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Could not open path " + request->path + ": " + e.what());
		}
	};
	server.start();
	return 0;
}