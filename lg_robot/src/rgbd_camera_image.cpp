#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <cmath>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <thread>

#include <gst/gst.h>
#include <gst/app/app.h>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;


class HeadCameraImageNode : public rclcpp::Node
{
public:
    HeadCameraImageNode(const std::string &host, const std::string &port, const std::string &endpoint, const std::string &mime)
        : Node("head_camera_image_node"), host_(host), port_(port), endpoint_(endpoint), mime_(mime)
    {
        gst_init(nullptr, nullptr);
        InitVideo();
        
        image_subscriber_ = this->create_subscription<sensor_msgs::msg::Image>(
            "/camera/rgb", 10,
            std::bind(&HeadCameraImageNode::image_callback, this, std::placeholders::_1));
        
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

    ~HeadCameraImageNode()
    {
        if (ws_) {
            ws_->close(websocket::close_code::normal);
        }
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
    }

    void InitVideo() {
        // const char* pipeline_desc = "appsrc name=src do-timestamp=true is-live=true emit-signals=true format=time caps=video/x-raw,format=RGB,width=1280,height=720 ! tee name=t t. ! queue ! autovideosink sync=false t. ! queue ! videoconvert ! nvvidconv ! nvh264enc bitrate=4000 min-force-key-unit-interval=1000000000 ! video/x-h264, profile=high, alignment=au, stream-format=byte-stream ! h264parse config-interval=1 ! queue leaky=2 ! appsink name=sink sync=false drop=true emit-signals=true max-buffers=3";
        const char* pipeline_desc = "appsrc name=src do-timestamp=true is-live=true emit-signals=true format=time caps=video/x-raw,format=RGB,width=1280,height=720 ! videoconvert ! openh264enc bitrate=4000000 gop-size=30 ! video/x-h264, profile=high, alignment=au, stream-format=byte-stream ! h264parse config-interval=1 ! queue leaky=2 ! appsink name=sink sync=false drop=true emit-signals=true max-buffers=3";
        pipeline = gst_parse_launch(pipeline_desc, nullptr);
        appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "src");
        appsink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
        g_signal_connect(appsink, "new-sample", G_CALLBACK(on_new_sample_static), this);
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
    }

    static GstFlowReturn on_new_sample_static(GstElement *sink, gpointer user_data)
    {
        return static_cast<HeadCameraImageNode*>(user_data)->on_new_sample(sink);
    }
    
    GstFlowReturn on_new_sample(GstElement* appsink)
    {
        GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink));
        if (!sample) {
            RCLCPP_ERROR(this->get_logger(), "Failed to pull sample from appsink");
            return GST_FLOW_ERROR;
        }

        GstBuffer* buffer = gst_sample_get_buffer(sample);
        if (!buffer) {
            RCLCPP_ERROR(this->get_logger(), "Failed to get buffer from sample");
            gst_sample_unref(sample);
            return GST_FLOW_ERROR;
        }

        // Convert the buffer to a string
        GstMapInfo map_info;
        if (!gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
            RCLCPP_ERROR(this->get_logger(), "Failed to map buffer");
            gst_sample_unref(sample);
            return GST_FLOW_ERROR;
        }
        
        // Send the encoded data over WebSocket
        try {
            ws_->write(net::buffer(map_info.data, map_info.size));
        } catch (std::exception& e) {
            RCLCPP_ERROR(this->get_logger(), "WebSocket write error: %s", e.what());
        }

        gst_buffer_unmap(buffer, &map_info);
        gst_sample_unref(sample);
        // Don't unref buffer here - it's owned by the sample

        return GST_FLOW_OK;
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


    void image_callback(const sensor_msgs::msg::Image::SharedPtr msg)
    {
        // push image data to GStreamer appsrc
        std::cout << "Received image with size: " << msg->data.size() << std::endl;
        GstBuffer* buffer = gst_buffer_new_allocate(nullptr, msg->data.size(), nullptr);
        gst_buffer_fill(buffer, 0, msg->data.data(), msg->data.size());
        GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(msg->header.stamp.sec, GST_SECOND, 1) + 
                                 gst_util_uint64_scale(msg->header.stamp.nanosec, GST_SECOND, 1000000000);
        GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(1, GST_SECOND, 30); // Assuming 30 FPS
        
        // Use proper AppSrc API instead of signal emission
        GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);
        if (ret != GST_FLOW_OK) {
            RCLCPP_ERROR(this->get_logger(), "Failed to push buffer to appsrc: %s", 
                        gst_flow_get_name(ret));
        }
        // Don't unref the buffer - gst_app_src_push_buffer takes ownership
    }

private:
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_subscriber_;    

    net::io_context ioc_;
    std::shared_ptr<websocket::stream<tcp::socket>> ws_;
    std::thread io_thread_;
    std::string host_;
    std::string port_;
    std::string endpoint_;
    std::string mime_;

    GstElement* pipeline;
    GstElement* appsrc;
    GstElement* appsink;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    std::string host = "";
    std::string port = "";
    std::string endpoint = "";
    std::string mime = "video/h264;width=1280;height=720;framerate=30/1;codecs=avc1.42002A";

    auto node = std::make_shared<HeadCameraImageNode>(host, port, endpoint, mime);
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
