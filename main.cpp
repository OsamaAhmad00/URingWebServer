#include <cstring>
#include <iostream>
#include <vector>

#include <liburing.h>
#include <netinet/in.h>

static std::vector<io_uring*> rings;

[[noreturn]] static void fatal_error(const std::string& message) {
    std::perror(message.c_str());
    std::exit(1);
}

template <template <typename> typename Alloc = std::allocator>
class Server : private Alloc<uint8_t> {

    // TODO should this be a member variable or template param?
    constexpr static size_t READ_BUFFER_SIZE = 4096;

    using alloc_traits = std::allocator_traits<Alloc<uint8_t>>;

    enum class EventType {
        Accept,
        Read,
        Write
    };

    struct Request {
        EventType type;

        int client_fd;

        // TODO remove this?
        sockaddr_in client_address;
        socklen_t client_addr_len;

        // TODO allow multiple iovecs
        // You might merge the single iovec and the
        //  request structs to have a single allocation

        // TODO, now we always assume that the size of each
        //  vector is READ_BUFFER_SIZE. Make it more flexible
        // size_t iovec_count;
        iovec iovecs[1];
    };


public:

    explicit Server(
        const uint32_t ip_address = INADDR_ANY,  // 0x00000000
        const uint16_t port = 8080,
        const uint32_t uring_queue_depth = 256,
        const int accept_queue_size =10
    ) : ip_address(ip_address), port(port), uring_queue_depth(uring_queue_depth), accept_queue_size(accept_queue_size)
    {
        io_uring_queue_init(uring_queue_depth, &ring, IORING_SETUP_SQPOLL);

        // To be cleaned up upon a shutting down signal
        rings.push_back(&ring);
    }

private:

    template <typename T>
    T* allocate(size_t len_in_bytes) {
        // TODO return an aligned address
        auto addr = alloc_traits::allocate(*this, len_in_bytes);
        memset(addr, 0, len_in_bytes);
        return reinterpret_cast<T*>(addr);
    }

    template <typename T>
    void dealloc(T* addr, size_t len) {
        auto casted = reinterpret_cast<uint8_t*>(addr);
        alloc_traits::deallocate(*this, casted, len);
    }

    void notify_update() {
        // TODO try letting the kernel poll instead
        io_uring_submit(&ring);
    }

    void setup_listening_socket() {
        listening_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (listening_socket == -1) {
            fatal_error("Listening socket creation failed");
        }

        int enable = true;
        auto result = setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
        if (result == -1) {
            fatal_error("setsockopt(SO_REUSEADDR) failed");
        }

        sockaddr_in server_address { };  // Already zeroed-out
        server_address.sin_family = AF_INET;
        server_address.sin_addr.s_addr = htonl(ip_address);
        server_address.sin_port = htons(port);

        auto casted = reinterpret_cast<sockaddr*>(&server_address);
        result = bind(listening_socket, casted, sizeof(server_address));
        if (result == -1) {
            fatal_error("bind() failed");
        }

        if (listen(listening_socket, accept_queue_size) == -1) {
            fatal_error("listen() failed");
        }
    }

    template <typename Func>
    void push_request(Request* request, Func&& func) {
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        func(sqe, request);
        io_uring_sqe_set_data(sqe, (void*)request);
        notify_update();
    }

    void push_accept_request(Request* request) {
        return push_request(request, [this](io_uring_sqe* sqe, Request* request) {
            io_uring_prep_accept(
                sqe,
                listening_socket,
                (sockaddr*)(&request->client_address),
                &(request->client_addr_len),
                0
            );
        });
    }

    void push_read_request(Request* request) {
        return push_request(request, [this](io_uring_sqe* sqe, Request* request) {
            io_uring_prep_readv(sqe, request->client_fd, request->iovecs, 1, 0);
        });
    }

    void push_write_request(Request* request) {
        return push_request(request, [this](io_uring_sqe* sqe, Request* request) {
            io_uring_prep_writev(sqe, request->client_fd, request->iovecs, 1, 0);
        });
    }

    void handle_accept_result(Request* request, int result) {
        request->client_fd = result;

        // After accept, we're going to read from the client, reusing the request object
        request->type = EventType::Read;
        auto buffer = allocate<char>(READ_BUFFER_SIZE);
        request->iovecs[0] = { .iov_base = (void*)buffer, .iov_len = READ_BUFFER_SIZE };
        push_read_request(request);

        // TODO handle this later. The number of requests keeps growing forever
        // Push another accept request for other connections
        auto new_request = allocate<Request>(sizeof(Request));
        new_request->type = EventType::Accept;
        push_accept_request(request);
    }

    void handle_read_result(Request* request, int result) {
        std::cout << "Result = " << result << std::endl;
        auto buffer = (char*)(request->iovecs[0].iov_base);
        buffer[result] = '\0';

        std::cout << "Read " << result << " bytes" << std::endl;
        std::cout << buffer << '\n';

        // handle_http_request(buffer);

        request->type = EventType::Write;
        std::copy(http_404_content, http_404_content + strlen(http_404_content), buffer);
        request->iovecs[0].iov_len = strlen(http_404_content);
        push_write_request(request);
    }

    void handle_write_result(Request* request, int result) {
        // TODO Have a better alternative here
        // Once a write ends, we close the connection, and reuse
        //  the request object for accepting another connection
        close(request->client_fd);
        request->type = EventType::Accept;
        push_accept_request(request);
    }

    void handle_result(Request* request, int result) {
        switch (request->type) {
            case EventType::Accept:
                request->client_fd = result;
                return handle_accept_result(request, result);
            case EventType::Read:

                return handle_read_result(request, result);
            case EventType::Write:
                return handle_write_result(request, result);
            default:
                fatal_error("Unknown request type");
        }
    }

    constexpr std::string evnet_str(EventType type) {
        switch (type) {
            case EventType::Accept:
                return "Accept";
            case EventType::Read:
                return "Read";
            case EventType::Write:
                return "Write";
        }
        fatal_error("Unknown event type");
    }

public:

    [[noreturn]] void run() {

        setup_listening_socket();

        // Initially, push an accept request
        auto request = allocate<Request>(sizeof(Request));
        request->type = EventType::Accept;
        push_accept_request(request);

        io_uring_cqe* cqe { };
        while (true) {
            if (io_uring_wait_cqe(&ring, &cqe) < 0) {
                fatal_error("io_uring_wait_cqe() failed");
            }

            auto request = (Request*)(io_uring_cqe_get_data(cqe));

            int result = cqe->res;
            if (result < 0) {
                std::cerr << "Async request failed: " << strerror(-result)
                    << ", for event " << evnet_str(request->type) << std::endl;
                std::cerr << "Request: " << request->client_fd << std::endl;
                // exit(1);
            } else {
                std::cout << "Handling " << evnet_str(request->type) << " for "
                  << request->client_fd << " with res=" << cqe->res << std::endl;
                handle_result(request, result);
            }

            io_uring_cqe_seen(&ring, cqe);

            // TODO will we ever deallocate requests, or will just keep resuing?
            // dealloc(request, sizeof(request));
        }
    }

private:

    uint32_t ip_address;
    uint16_t port;

    io_uring ring{};
    uint32_t uring_queue_depth;

    int accept_queue_size;

    int listening_socket = -1;

    constexpr static auto unimplemented_content =
        "HTTP/1.0 400 Bad Request\r\n"
        "Content-type: text/html\r\n"
        "\r\n"
        "<html>"
        "<head>"
        "<title>URingWebServer: Unimplemented</title>"
        "</head>"
        "<body>"
        "<h1>Bad Request (Unimplemented)</h1>"
        "<p>Your client sent a request URingWebServer did not understand and it is probably not your fault.</p>"
        "</body>"
        "</html>";

    constexpr static auto http_404_content =
        "HTTP/1.0 404 Not Found\r\n"
        "Content-type: text/html\r\n"
        "\r\n"
        "<html>"
        "<head>"
        "<title>URingWebServer: Not Found</title>"
        "</head>"
        "<body>"
        "<h1>Not Found (404)</h1>"
        "<p>Your client is asking for an object that was not found on this server.</p>"
        "</body>"
        "</html>";
};

void sigint_handler(int) {
    printf("Shutting down...\n");
    for (const auto ring : rings) {
        io_uring_queue_exit(ring);
    }
    exit(0);
}

int main() {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    Server { INADDR_ANY, 8005 }.run();
}