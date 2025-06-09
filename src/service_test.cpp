#include <rclcpp/rclcpp.hpp>
#include <lg_robot/srv/gripper_command.hpp> // Include your generated service header

// Define the service callback function
void handle_gripper_command(
  const std::shared_ptr<lg_robot::srv::GripperCommand::Request> request,
  std::shared_ptr<lg_robot::srv::GripperCommand::Response> response)
{
  // Log the received command
  RCLCPP_INFO(rclcpp::get_logger("gripper_service_node"),
              "Received gripper command: %d", request->command);

  // --- Implement your gripper control logic here ---
  // For demonstration, we'll just acknowledge the command.

  if (request->command == 1) { // Example: '1' could mean close gripper
    response->result = true;
    response->message = "Gripper closing initiated.";
    RCLCPP_INFO(rclcpp::get_logger("gripper_service_node"), "Closing gripper.");
  } else if (request->command == 0) { // Example: '0' could mean open gripper
    response->result = false;
    response->message = "Gripper opening initiated.";
    RCLCPP_INFO(rclcpp::get_logger("gripper_service_node"), "Opening gripper.");
  } else {
    response->result = false;
    response->message = "Invalid gripper command.";
    RCLCPP_WARN(rclcpp::get_logger("gripper_service_node"), "Received invalid command: %d", request->command);
  }
}

int main(int argc, char **argv)
{
  rclcpp::init(argc, argv);
  std::shared_ptr<rclcpp::Node> node = rclcpp::Node::make_shared("my_gripper_service_node");

  // Create the service with the specified name and type
  rclcpp::Service<lg_robot::srv::GripperCommand>::SharedPtr service =
    node->create_service<lg_robot::srv::GripperCommand>(
      "/jodell/gripper_command/right", // The exact service name you requested
      &handle_gripper_command);

  RCLCPP_INFO(rclcpp::get_logger("gripper_service_node"),
              "Service '/jodell/gripper_command/right' of type 'lg_robot/srv/GripperCommand' is ready.");

  rclcpp::spin(node); // Keep the node alive to serve requests
  rclcpp::shutdown();
  return 0;
}