#pragma once
// Runtimepaden en een eenvoudige, blokkerende client voor de ravengund-socket.
#include <optional>
#include <string>

namespace ravengun::ipc {

// $XDG_RUNTIME_DIR/ravengun ; leeg als XDG_RUNTIME_DIR niet (absoluut) gezet is.
std::string runtime_dir();
std::string socket_path();  // <runtime_dir>/ravengund.sock
std::string lock_path();    // <runtime_dir>/ravengund.lock

class Client {
public:
    Client() = default;
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    // false bij een fout; error() geeft de reden.
    bool connect(const std::string& path, int timeout_ms = 1000);
    bool send_line(const std::string& line);
    // Leest één regel (zonder '\n'). nullopt bij EOF, fout of time-out (timeout_ms < 0 = oneindig).
    std::optional<std::string> read_line(int timeout_ms);
    const std::string& error() const { return err_; }
    void close();

private:
    int fd_ = -1;
    std::string buf_;
    std::string err_;
};

}  // namespace ravengun::ipc
