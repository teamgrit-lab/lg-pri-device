#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>
#include <std_msgs/msg/bool.hpp>

#include <nlohmann/json.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <thread>
#include <deque>

// #include <gripper_interfaces/srv/gripper_command.hpp>
#include <grp_control_msg/srv/single_int.hpp>
#include <grp_control_msg/srv/void.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

class RightArmNode : public rclcpp::Node
{
public:
    RightArmNode(const std::string &host, const std::string &port, const std::string &endpoint, const std::string &mime)
        : Node("right_arm_node"), host_(host), port_(port), endpoint_(endpoint), mime_(mime), tf_buffer_(this->get_clock()), tf_listener_(tf_buffer_)
    {
        pose_publisher_ = this->create_publisher<geometry_msgs::msg::PoseStamped>("/right_arm_pose", 10);
        pose_publisher_no_filter = this->create_publisher<geometry_msgs::msg::PoseStamped>("/right_arm_pose_no_filter", 10);
        sync_publisher_ = this->create_publisher<std_msgs::msg::Bool>("/right_arm_sync_trigger", 10);
        ai_mode_publisher_ = this->create_publisher<std_msgs::msg::Bool>("/ai_mode_trigger", 10);

//        client_ = this->create_client<gripper_interfaces::srv::GripperCommand>("/jodell/gripper_command/right");
        select_gripper_client_ = this->create_client<grp_control_msg::srv::SingleInt>("/modbus_slave_change");
        gripper_open_client_ = this->create_client<grp_control_msg::srv::Void>("/grp_open");
        gripper_close_client_ = this->create_client<grp_control_msg::srv::Void>("/grp_close");

        
        tf_send_timer_ = this->create_wall_timer(std::chrono::milliseconds(20), std::bind(&RightArmNode::lookup_tf_and_send, this));

        sync_publisher_timer_ = this->create_wall_timer(std::chrono::milliseconds(100), std::bind(&RightArmNode::sync_publisher_timer_callback, this));

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
            write_timer_ = std::make_shared<boost::asio::steady_timer>(ioc_, std::chrono::milliseconds(20));
            io_thread_ = std::thread([this]() { ioc_.run(); });
            read();
            write();
        }
        catch (std::exception &e) {
            RCLCPP_ERROR(this->get_logger(), "Error connecting to WebSocket: %s", e.what());
        }
    }

    ~RightArmNode()
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

    void call_service(const int request_data) {
        auto request = std::make_shared<grp_control_msg::srv::SingleInt::Request>();
        request->value = 2;
        auto gripper_request = std::make_shared<grp_control_msg::srv::Void::Request>();
    
        // Use async callback-based approach instead of spin_until_future_complete
        auto callback = [this](rclcpp::Client<grp_control_msg::srv::SingleInt>::SharedFuture future) {
            try {
                auto response = future.get();
//                RCLCPP_INFO(this->get_logger(), "Service call success: %s", response->message.c_str());
                // if (response->successed) {
                //     if (request_data > 0) {
                //         gripper_state_ = true;
                //     }
                //     else {
                //         gripper_state_ = false;
                //     }
                // }
//                RCLCPP_INFO(this->get_logger(), "Service call success: %s", response->message.c_str());
            } catch (const std::exception &e) {
                RCLCPP_ERROR(this->get_logger(), "Service call failed: %s", e.what());
            }
        };

        auto gripper_callback = [this, request_data](rclcpp::Client<grp_control_msg::srv::Void>::SharedFuture future) {
            try {
                auto response = future.get();
//                RCLCPP_INFO(this->get_logger(), "Service call success: %s", response->message.c_str());
                if (response->successed) {
                    if (request_data > 0) {
                        gripper_state_ = true;
                    }
                    else {
                        gripper_state_ = false;
                    }
                }
//                RCLCPP_INFO(this->get_logger(), "Service call success: %s", response->message.c_str());
            } catch (const std::exception &e) {
                RCLCPP_ERROR(this->get_logger(), "Service call failed: %s", e.what());
            }
        };
    
        // Send the request asynchronously
        auto future_result = select_gripper_client_->async_send_request(request, callback);
        if (request_data == 1) {
            auto open_result = gripper_open_client_->async_send_request(gripper_request, gripper_callback);
        } else {
            auto close_result = gripper_close_client_->async_send_request(gripper_request, gripper_callback);
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
            ws_->async_read(buffer_, beast::bind_front_handler(&RightArmNode::on_read, this));
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

            auto current_pose = std::make_shared<geometry_msgs::msg::PoseStamped>();
            current_pose->header.stamp = this->now();
            current_pose->header.frame_id = "base_link";
            current_pose->pose.position.x = -std::stof(j["position"]["z"].get<std::string>());
            current_pose->pose.position.y = -std::stof(j["position"]["x"].get<std::string>());
            current_pose->pose.position.z = std::stof(j["position"]["y"].get<std::string>()) + 1.1;
            current_pose->pose.orientation.x = -std::stof(j["orientation"]["z"].get<std::string>());
            current_pose->pose.orientation.y = -std::stof(j["orientation"]["x"].get<std::string>());
            current_pose->pose.orientation.z = std::stof(j["orientation"]["y"].get<std::string>());
            current_pose->pose.orientation.w = std::stof(j["orientation"]["w"].get<std::string>());

            pose_buffer_.push_back(current_pose);
            if (pose_buffer_.size() > 12) {
                pose_buffer_.pop_front();
            }

            geometry_msgs::msg::PoseStamped averaged_pose;
            averaged_pose.header = current_pose->header;

            double sum_x = 0.0;
            double sum_y = 0.0;
            double sum_z = 0.0;
            double sum_qx = 0.0;
            double sum_qy = 0.0;
            double sum_qz = 0.0;
            double sum_qw = 0.0;

            double sum_exp = 0.0;
            int count = 0;
            for (const auto& pose_msg : pose_buffer_) {
                sum_exp += std::exp((count / 12.0) - 1);
                sum_x += pose_msg->pose.position.x * std::exp((count / 12.0) - 1);
                sum_y += pose_msg->pose.position.y * std::exp((count / 12.0) - 1);
                sum_z += pose_msg->pose.position.z * std::exp((count / 12.0) - 1);

                count++;

//                sum_qx += pose_msg->pose.orientation.x;
//                sum_qy += pose_msg->pose.orientation.y;
//                sum_qz += pose_msg->pose.orientation.z;
//                sum_qw += pose_msg->pose.orientation.w;
            }

            averaged_pose.pose.position.x = sum_x / sum_exp;
            averaged_pose.pose.position.y = sum_y / sum_exp;
            averaged_pose.pose.position.z = sum_z / sum_exp;

            averaged_pose.pose.orientation = current_pose->pose.orientation;

//            averaged_pose.pose.orientation.x = sum_qx / pose_buffer_.size();
//            averaged_pose.pose.orientation.y = sum_qy / pose_buffer_.size();
//            averaged_pose.pose.orientation.z = sum_qz / pose_buffer_.size();
//            averaged_pose.pose.orientation.w = sum_qw / pose_buffer_.size();

//            double norm = std::sqrt(averaged_pose.pose.orientation.x * averaged_pose.pose.orientation.x + averaged_pose.pose.orientation.y * averaged_pose.pose.orientation.y + averaged_pose.pose.orientation.z * averaged_pose.pose.orientation.z + averaged_pose.pose.orientation.w * averaged_pose.pose.orientation.w);
//            if (norm != 0) {
//                averaged_pose.pose.orientation.x /= norm;
//                averaged_pose.pose.orientation.y /= norm;
//                averaged_pose.pose.orientation.z /= norm;
//                averaged_pose.pose.orientation.w /= norm;
//            }

            pose_publisher_->publish(averaged_pose);
            pose_publisher_no_filter->publish(*current_pose);


            try {
                int gripper = j["gripper"].get<int>();
                call_service(gripper);
            } catch (std::exception &e) {
                RCLCPP_ERROR(this->get_logger(), "Error parsing gripper state from JSON: %s", e.what());
            }

            try
            {
                if (j.contains("sync"))
                {
                    sync_ = j["sync"].get<bool>();
//                    auto sync_msg = std_msgs::msg::Bool();
//                    sync_msg.data = j["sync"].get<bool>();
//                    sync_publisher_->publish(sync_msg);
                }
                if (j.contains("ai_mode"))
                {
                    ai_mode_ = j["ai_mode"].get<bool>();
                }
            }
            catch (const json::exception &e)
            {
                RCLCPP_ERROR(this->get_logger(), "Error parsing sync from JSON: %s", e.what());
            }
        }
        catch (json::parse_error &e) {
            RCLCPP_ERROR(this->get_logger(), "Error parsing JSON: %s", e.what());
        }
        catch (std::exception &e) {
            RCLCPP_ERROR(this->get_logger(), "Error processing message: %s", e.what());
        }
        read();
    }

    void sync_publisher_timer_callback() {
        try {
            auto sync_msg = std_msgs::msg::Bool();
            sync_msg.data = sync_;
            sync_publisher_->publish(sync_msg);

            auto ai_mode_msg = std_msgs::msg::Bool();
            ai_mode_msg.data = ai_mode_;
            ai_mode_publisher_->publish(ai_mode_msg);

        } catch (const std::exception& e) {
            RCLCPP_ERROR(this->get_logger(), "Error sync_publisher: %s", e.what());
        }
    }

    void lookup_tf_and_send() {
        try{
            geometry_msgs::msg::TransformStamped transform_stamped = tf_buffer_.lookupTransform("base_link", "wrist3_link_right", tf2::TimePointZero);

            json j_tf_data;
            j_tf_data["type"] = "type_pose";
            j_tf_data["handedness"] = "right";
            j_tf_data["position"]["x"] = -transform_stamped.transform.translation.y;
            j_tf_data["position"]["y"] = transform_stamped.transform.translation.z - 1.1;
            j_tf_data["position"]["z"] = -transform_stamped.transform.translation.x;
            j_tf_data["orientation"]["x"] = -transform_stamped.transform.rotation.y;
            j_tf_data["orientation"]["y"] = transform_stamped.transform.rotation.z;
            j_tf_data["orientation"]["z"] = -transform_stamped.transform.rotation.x;
            j_tf_data["orientation"]["w"] = transform_stamped.transform.rotation.w;
            j_tf_data["gripper"] = gripper_state_;

            tf_json_str = j_tf_data.dump();
            if (!trigger){
                trigger = true;
            }
//            ws_->async_write(net::buffer(tf_json_str), beast::bind_front_handler(&RightArmNode::on_tf_write, this));
        } catch (const tf2::TransformException &ex) {
            RCLCPP_WARN(this->get_logger(), "Could not transform base_link to wrist3_link_right: %s", ex.what());
        } catch (const std::exception& e) {
            RCLCPP_ERROR(this->get_logger(), "Error tf lookup: %s", e.what());
        }
    }

    void on_tf_write(beast::error_code ec, std::size_t bytes_transferred) {
//        tf_is_running_ = false;
        if (ec) {
            RCLCPP_ERROR(this->get_logger(), "Error writing TF to WebSocket: %s", ec.message().c_str());
        } else {
            RCLCPP_DEBUG(this->get_logger(), "Successfully sent TF data: %zu bytes", bytes_transferred);
        }
    }

    void write()
    {
        write_timer_->expires_after(std::chrono::milliseconds(20));
        if (trigger) {
            trigger = false;
            write();
            return;
        }


        write_timer_->async_wait([this](const beast::error_code &ec) {
            if (ec) {
                RCLCPP_ERROR(this->get_logger(), "Error in write timer: %s", ec.message().c_str());
                reconnect();
                write();
                return;
            }
            ws_->async_write(net::buffer(tf_json_str), beast::bind_front_handler(&RightArmNode::on_write, this));
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
    rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr pose_publisher_no_filter;
    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr sync_publisher_;
    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr ai_mode_publisher_;
    tf2_ros::Buffer tf_buffer_;
    tf2_ros::TransformListener tf_listener_;
    rclcpp::TimerBase::SharedPtr tf_send_timer_;
//    rclcpp::Client<gripper_interfaces::srv::GripperCommand>::SharedPtr client_;
    rclcpp::Client<grp_control_msg::srv::SingleInt>::SharedPtr select_gripper_client_;
    rclcpp::Client<grp_control_msg::srv::Void>::SharedPtr gripper_open_client_;
    rclcpp::Client<grp_control_msg::srv::Void>::SharedPtr gripper_close_client_;

    rclcpp::TimerBase::SharedPtr sync_publisher_timer_;

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
    bool trigger = false;
    bool gripper_state_ = false;
    bool sync_ = false;
    bool ai_mode_ = false;

    std::string tf_json_str = "";

//    bool tf_is_running_ = false;


    std::deque<geometry_msgs::msg::PoseStamped::SharedPtr> pose_buffer_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    std::string host = "";
    std::string port = "";
    std::string endpoint = "";
    std::string mime = "text/json";

    auto node = std::make_shared<RightArmNode>(host, port, endpoint, mime);
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
