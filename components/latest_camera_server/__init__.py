import esphome.codegen as cg
from esphome.components import socket
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT

DEPENDENCIES = ["network", "esp32_camera"]
AUTO_LOAD = ["camera", "socket"]
DOMAIN = "latest_camera_server"

camera_ns = cg.esphome_ns.namespace("camera")
latest_camera_server_ns = cg.esphome_ns.namespace(DOMAIN)
LatestCameraServer = latest_camera_server_ns.class_(
    "LatestCameraServer", cg.Component, camera_ns.class_("CameraListener")
)


def _consume_sockets(config):
    socket.consume_sockets(2, DOMAIN)(config)
    socket.consume_sockets(1, DOMAIN, socket.SocketType.TCP_LISTEN)(config)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LatestCameraServer),
            cv.Optional(CONF_PORT, default=8082): cv.port,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _consume_sockets,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_PORT])
    await cg.register_component(var, config)
