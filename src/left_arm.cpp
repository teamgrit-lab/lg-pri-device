#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>

#include <nlohmann/json.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <thread>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

class LeftArmNode : public rclcpp::Node
{
public:
    LeftArmNode(const std::string &host, const std::string &port, const std::string &endpoint, const std::string &mime)
        : Node("left_arm_node"), host_(host), port_(port), endpoint_(endpoint), mime_(mime)
    {
        pose_publisher_ = this->create_publisher<geometry_msgs::msg::PoseStamped>("/left_arm_pose", 10);
        
        // Start the WebSocket connection
        try {
            tcp::resolver resolver(ioc_);
            ws_ = std::make_shared<websocket::stream<tcp::socket>>(ioc_);
            auto const results = resolver.resolve(host_, port_);
            auto ep = net::connect(ws_->next_layer(), results);

            std::string host__ = host_ + ':' + std::to_string(ep.port());
            ws_->set_option(websocket::stream_base::decorator(
                [host__](websocket::request_type &req) {
                    req.set(http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-client-coro");
                }));
            ws_->handshake(host__, endpoint_);
            ws_->binary(false);
            ws_->write(net::buffer(mime_));
            ws_->binary(true);
            read_timer_ = std::make_shared<boost::asio::steady_timer>(ioc_, std::chrono::milliseconds(10));
            write_timer_ = std::make_shared<boost::asio::steady_timer>(ioc_, std::chrono::seconds(10));
            io_thread_ = std::thread([this]() { ioc_.run(); });
            read();
            write();
        }
        catch (std::exception &e) {
            RCLCPP_ERROR(this->get_logger(), "Error connecting to WebSocket: %s", e.what());
        }
    }

    ~LeftArmNode()
    {
        if (ws_) {
            ws_->close(websocket::close_code::normal);
        }
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
    }

    void reconnect()
    {
        try {
            tcp::resolver resolver(ioc_);
            ws_ = std::make_shared<websocket::stream<tcp::socket>>(ioc_);
            auto const results = resolver.resolve(host_, port_);
            auto ep = net::connect(ws_->next_layer(), results);

            std::string host__ = host_ + ':' + std::to_string(ep.port());
            ws_->set_option(websocket::stream_base::decorator(
                [host__](websocket::request_type &req) {
                    req.set(http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-client-coro");
                }));
            ws_->handshake(host__, endpoint_);
            ws_->binary(false);
            ws_->write(net::buffer(mime_));
            ws_->binary(true);
        }
        catch (std::exception &e) {
            RCLCPP_ERROR(this->get_logger(), "Error reconnecting to WebSocket: %s", e.what());
        }
    }

    void read()
    {
        read_timer_->expires_after(std::chrono::milliseconds(10));
        read_timer_->async_wait([this](const beast::error_code &ec) {
            if (ec) {
                RCLCPP_ERROR(this->get_logger(), "Error in read timer: %s", ec.message().c_str());
                reconnect();
                read();
                return;
            }
            ws_->async_read(buffer_, beast::bind_front_handler(&LeftArmNode::on_read, this));
        });
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred)
    {
        if (ec) {
            RCLCPP_ERROR(this->get_logger(), "Error reading from WebSocket: %s", ec.message().c_str());
            reconnect();
            read();
            return;
        }

        std::string message = beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());

        try {
            json j = json::parse(message);
            auto pose_msg = std::make_shared<geometry_msgs::msg::PoseStamped>();
            pose_msg->header.stamp = this->now();
            pose_msg->header.frame_id = "map";
            pose_msg->pose.position.x = -std::stof(j["position"]["z"].get<std::string>());
            pose_msg->pose.position.y = -std::stof(j["position"]["x"].get<std::string>());
            pose_msg->pose.position.z = std::stof(j["position"]["y"].get<std::string>());
            pose_msg->pose.orientation.x = -std::stof(j["orientation"]["z"].get<std::string>());
            pose_msg->pose.orientation.y = -std::stof(j["orientation"]["x"].get<std::string>());
            pose_msg->pose.orientation.z = std::stof(j["orientation"]["y"].get<std::string>());
            pose_msg->pose.orientation.w = std::stof(j["orientation"]["w"].get<std::string>());
            pose_publisher_->publish(*pose_msg);
        }
        catch (json::parse_error &e) {
            RCLCPP_ERROR(this->get_logger(), "Error parsing JSON: %s", e.what());
        }
        catch (std::exception &e) {
            RCLCPP_ERROR(this->get_logger(), "Error processing message: %s", e.what());
        }
        read();
    }

    void write()
    {
        write_timer_->expires_after(std::chrono::seconds(10));
        write_timer_->async_wait([this](const beast::error_code &ec) {
            if (ec) {
                RCLCPP_ERROR(this->get_logger(), "Error in write timer: %s", ec.message().c_str());
                reconnect();
                write();
                return;
            }
            std::string ping = "ping";
            ws_->async_write(net::buffer(ping), beast::bind_front_handler(&LeftArmNode::on_write, this));
        });
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred)
    {
        if (ec) {
            RCLCPP_ERROR(this->get_logger(), "Error writing to WebSocket: %s", ec.message().c_str());
            reconnect();
            write();
            return;
        }
        write();
    }

private:
    rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr pose_publisher_;

    net::io_context ioc_;
    std::shared_ptr<websocket::stream<tcp::socket>> ws_;
    std::thread io_thread_;
    std::string host_;
    std::string port_;
    std::string endpoint_;
    std::string mime_;
    std::shared_ptr<net::steady_timer> read_timer_;
    std::shared_ptr<net::steady_timer> write_timer_;
    beast::flat_buffer buffer_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    std::string host = "";
    std::string port = "";
    std::string endpoint = "";
    std::string mime = "text/json";

    auto node = std::make_shared<LeftArmNode>(host, port, endpoint, mime);
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
