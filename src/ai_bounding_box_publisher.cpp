#include <rclcpp/rclcpp.hpp>
#include <lg_robot/msg/detection_array.hpp>

#include <nlohmann/json.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <thread>
#include <deque>

#include <lg_robot/srv/gripper_command.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

class AiBoundingBoxPublisher : public rclcpp::Node
{
public:
    AiBoundingBoxPublisher(const std::string &host, const std::string &port, const std::string &endpoint, const std::string &mime)
        : Node("ai_bounding_box_publisher_node"), host_(host), port_(port), endpoint_(endpoint), mime_(mime)
    {
        detection_subscriber_ = this->create_subscription<lg_robot::msg::DetectionArray>(
            "/ai_bounding_boxes", 10, std::bind(&AiBoundingBoxPublisher::detection_callback, this, std::placeholders::_1));
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
            write_timer_ = std::make_shared<boost::asio::steady_timer>(ioc_, std::chrono::milliseconds(20));
            io_thread_ = std::thread([this]() { ioc_.run(); });
            read();
            write();
        }
        catch (std::exception &e) {
            RCLCPP_ERROR(this->get_logger(), "Error connecting to WebSocket: %s", e.what());
        }
    }

    ~AiBoundingBoxPublisher()
    {
        if (ws_) {
            ws_->close(websocket::close_code::normal);
        }
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
    }

    void detection_callback(const lg_robot::msg::DetectionArray::SharedPtr msg)
    {
        if (msg->detections.empty()) {
            return;
        }
        // { "objects": [{ "type": "rect", "x": 10, "y": 10, "width": 50, "height": 50, "color": "blue" }] }

        json j;
        j["objects"] = json::array();
        for (const auto &detection : msg->detections) {
            json obj;
            obj["class_name"] = detection.class_name;
            obj["score"] = detection.score;

            obj["bbox"]["size"]["x"] = detection.bbox.size.x;
            obj["bbox"]["size"]["y"] = detection.bbox.size.y;
            obj["bbox"]["center"]["position"]["x"] = detection.bbox.center.position.x;
            obj["bbox"]["center"]["position"]["y"] = detection.bbox.center.position.y;
            obj["bbox"]["center"]["theta"] = detection.bbox.center.theta;
            j["objects"].push_back(obj);
        }
        bounding_box_ = j.dump();
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
        read_timer_->expires_after(std::chrono::milliseconds(100));
        read_timer_->async_wait([this](const beast::error_code &ec) {
            if (ec) {
                RCLCPP_ERROR(this->get_logger(), "Error in read timer: %s", ec.message().c_str());
                reconnect();
                read();
                return;
            }
            ws_->async_read(buffer_, beast::bind_front_handler(&AiBoundingBoxPublisher::on_read, this));
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
        write_timer_->expires_after(std::chrono::milliseconds(33));

        write_timer_->async_wait([this](const beast::error_code &ec) {
            if (ec) {
                RCLCPP_ERROR(this->get_logger(), "Error in write timer: %s", ec.message().c_str());
                reconnect();
                write();
                return;
            }
            ws_->async_write(net::buffer(bounding_box_), beast::bind_front_handler(&AiBoundingBoxPublisher::on_write, this));
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
    rclcpp::Subscription<lg_robot::msg::DetectionArray>::SharedPtr detection_subscriber_;
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

    std::string bounding_box_;

};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    std::string host = "";
    std::string port = "";
    std::string endpoint = "";
    std::string mime = "text/json";

    auto node = std::make_shared<AiBoundingBoxPublisher>(host, port, endpoint, mime);
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
