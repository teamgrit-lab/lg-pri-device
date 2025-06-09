#include <rclcpp/rclcpp.hpp>

#include <sensor_msgs/msg/point_cloud2.hpp>
#include <sensor_msgs/point_cloud2_iterator.hpp>
#include <cmath>

#include <draco/compression/encode.h>
#include <draco/point_cloud/point_cloud.h>
#include <draco/point_cloud/point_cloud_builder.h>
#include <limits>

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


class HeadCameraNode : public rclcpp::Node
{
public:
    HeadCameraNode(const std::string &host, const std::string &port, const std::string &endpoint, const std::string &mime)
        : Node("head_camera_node"), host_(host), port_(port), endpoint_(endpoint), mime_(mime)
    {
        point_cloud_subscriber_ = this->create_subscription<sensor_msgs::msg::PointCloud2>(
            "/camera/camera/depth/color/points", 10,
            std::bind(&HeadCameraNode::point_cloud_callback, this, std::placeholders::_1));
        
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
            io_thread_ = std::thread([this]() { ioc_.run(); });
        }
        catch (std::exception &e) {
            RCLCPP_ERROR(this->get_logger(), "Error connecting to WebSocket: %s", e.what());
        }
    }

    ~HeadCameraNode()
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

    void point_cloud_callback(const sensor_msgs::msg::PointCloud2::SharedPtr msg)
    {
        // Create a draco point cloud builder
        draco::PointCloudBuilder builder;
        const uint32_t num_points = msg->width * msg->height;
        builder.Start(num_points);

        // Add position attribute
        int pos_att_id = builder.AddAttribute(draco::GeometryAttribute::POSITION, 
                                             3, 
                                             draco::DataType::DT_FLOAT32);
        
        // Add color attribute if needed
        int color_att_id = builder.AddAttribute(draco::GeometryAttribute::COLOR, 
                                              3, 
                                              draco::DataType::DT_UINT8);
        
        // Create buffers for point data
        std::vector<float> point_data(num_points * 3);
        std::vector<uint8_t> color_data(num_points * 3);
        
        // Access point data directly from the raw buffer
        uint32_t valid_points = 0;
        for (uint32_t i = 0; i < num_points; ++i) {
            // Get position data
            float x, y, z;
            memcpy(&x, &msg->data[i * msg->point_step + msg->fields[0].offset], sizeof(float));
            memcpy(&y, &msg->data[i * msg->point_step + msg->fields[1].offset], sizeof(float));
            memcpy(&z, &msg->data[i * msg->point_step + msg->fields[2].offset], sizeof(float));
            
            // Skip invalid points
            if (std::isnan(x) || std::isnan(y) || std::isnan(z)) {
                continue;
            }
            
            // Store position
            point_data[valid_points * 3] = x;
            point_data[valid_points * 3 + 1] = y;
            point_data[valid_points * 3 + 2] = z;
            
            // Get color data (rgb field at offset 16)
            float rgb_float;
            memcpy(&rgb_float, &msg->data[i * msg->point_step + msg->fields[3].offset], sizeof(float));
            uint32_t rgb = *reinterpret_cast<uint32_t*>(&rgb_float);
            
            // Extract RGB components
            color_data[valid_points * 3] = (rgb >> 16) & 0xFF;     // R
            color_data[valid_points * 3 + 1] = (rgb >> 8) & 0xFF;  // G
            color_data[valid_points * 3 + 2] = rgb & 0xFF;         // B
            
            valid_points++;
        }
        
        // If we have fewer valid points than total points, resize the buffers
        if (valid_points < num_points) {
            point_data.resize(valid_points * 3);
            color_data.resize(valid_points * 3);
            builder.Start(valid_points);  // Restart with correct number of points
        }
        
        // Set attribute values for all points at once
        builder.SetAttributeValuesForAllPoints(pos_att_id, point_data.data(), sizeof(float) * 3);
        builder.SetAttributeValuesForAllPoints(color_att_id, color_data.data(), sizeof(uint8_t) * 3);
        
        // Create the point cloud
        std::unique_ptr<draco::PointCloud> encoded_pc = builder.Finalize(false);
        
        // Encode the point cloud with optimized settings
        draco::Encoder encoder;
        encoder.SetSpeedOptions(10, 10);  // Faster compression
        encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION, 5);  // Position precision
        encoder.SetAttributeQuantization(draco::GeometryAttribute::COLOR, 3);      // Color precision
        
        // Encode to buffer
        draco::EncoderBuffer buffer;
        draco::Status status = encoder.EncodePointCloudToBuffer(*encoded_pc, &buffer);
        if (!status.ok()) {
            // RCLCPP_ERROR(this->get_logger(), "Error encoding point cloud: %s", 
            //             status.error_msg().c_str());
            return;
        }

        // Get encoded data as string
        std::string encoded_data;
        encoded_data.assign(buffer.data(), buffer.size());

        ws_->write(net::buffer(encoded_data));
    }

private:
    rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr point_cloud_subscriber_;    

    net::io_context ioc_;
    std::shared_ptr<websocket::stream<tcp::socket>> ws_;
    std::thread io_thread_;
    std::string host_;
    std::string port_;
    std::string endpoint_;
    std::string mime_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    std::string host = "";
    std::string port = "";
    std::string endpoint = "";
    std::string mime = "lidar/draco";

    auto node = std::make_shared<HeadCameraNode>(host, port, endpoint, mime);
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
