// Copyright 2018 Your Name <your_email>

#include <header.hpp>

boost::recursive_mutex mutex;
boost::asio::io_service service;
class talk_to_client;
std::vector<std::shared_ptr<talk_to_client>> clients;

class talk_to_client
    : boost::enable_shared_from_this<talk_to_client> {
private:
    boost::asio::ip::tcp::socket sock_;
    size_t already_read_;
    char buff_[MAX_MSG];
    std::string username_;
    bool clients_changed_;
    boost::posix_time::ptime last_ping;
public:
    talk_to_client()
        : sock_(service), already_read_(0),
         last_ping(boost::posix_time::microsec_clock::local_time())
    {}

    std::string username() const {
        return username_;
    }

    void answer_to_client() {
        try {
            read_request();
            process_request();
        } catch (boost::system::system_error&) {
            stop();
        }
        if (timed_out()) {
            stop();
            std::cout << "finishing " << username()
             << " - inaction" << std::endl;
        }
    }

    boost::asio::ip::tcp::socket& sock() {
        return sock_;
    }

    bool timed_out() const {
        boost::posix_time::ptime now =
         boost::posix_time::microsec_clock::local_time();
        int64_t ms = (now - last_ping).total_milliseconds();
        return ms > 5000;
    }

    void stop() {
        boost::system::error_code err;
        sock_.close(err);
    }

    void read_request() {
        if (sock_.available())
            already_read_ += sock_.read_some(
             boost::asio::buffer(
              buff_ + already_read_, MAX_MSG - already_read_));
    }

    void process_request() {
        bool found_enter = std::find(buff_, buff_ + already_read_, '\n')
         < buff_ + already_read_;
        if (!found_enter) return;
        last_ping = boost::posix_time::microsec_clock::local_time();
        size_t pos = std::find(buff_, buff_ + already_read_, '\n') - buff_;
        std::string msg(buff_, pos);
        std::copy(buff_ + already_read_, buff_ + MAX_MSG, buff_);
        already_read_ = 0;
        if (msg.find("login ") == 0) {
            on_login(msg);
        } else if (msg.find("ping") == 0) {
            on_ping();
        } else if (msg.find("ask_clients") == 0) {
            on_clients();
        } else {
            std::cerr << "invalid msg " << msg << std::endl;
        }
    }

    void on_login(const std::string & msg) {
        std::istringstream in(msg);
        in >> username_ >> username_;
        std::cout << username_ << " logged in" << std::endl;
        write("login ok\n");
        update_clients_changed();
    }

    void update_clients_changed() {
        clients_changed_ = true;
    }

    void on_ping() {
        write(clients_changed_ ? "ping client_list_changed\n" : "ping ok\n");
        clients_changed_ = false;
    }

    void on_clients() {
        std::string msg;
        boost::recursive_mutex::scoped_lock lk(mutex);
            for (auto & client : clients)
                msg += client->username() + " ";
        write("clients " + msg + "\n");
    }

    void write(const std::string & msg) {
        sock_.write_some(boost::asio::buffer(msg));
    }
};

void accept_thread() {
    boost::asio::ip::tcp::acceptor acceptor{
     service, boost::asio::ip::tcp::endpoint{
      boost::asio::ip::tcp::v4(), 8001}};
    while (true) {
        auto client = std::make_shared<talk_to_client>();
        acceptor.accept(client->sock());
        BOOST_LOG_TRIVIAL(info) << "client accepted" << std::endl;
        boost::recursive_mutex::scoped_lock lock{mutex};
        clients.push_back(client);
    }
}

void handle_clients_thread() {
    while (true) {
        boost::recursive_mutex::scoped_lock lock{mutex};
        for (auto b = clients.begin(); b != clients.end(); ++b)
            (*b)->answer_to_client();
        clients.erase(std::remove_if(clients.begin(), clients.end(),
         boost::bind(&talk_to_client::timed_out, _1)), clients.end());
    }
}

void init_logging() {
    boost::log::add_file_log(
            boost::log::keywords::file_name = "info.log",
            boost::log::keywords::format =
                    "[%TimeStamp%] [%ThreadID%] %LineID% %Message%");
    boost::log::add_console_log(
            std::cout,
            boost::log::keywords::format =
                    "[%TimeStamp%] [%ThreadID%] %LineID% %Message%");
}

int main() {
    init_logging();
    boost::log::add_common_attributes();
    boost::thread_group threads;
    threads.create_thread(accept_thread);
    threads.create_thread(handle_clients_thread);
    threads.join_all();
}

