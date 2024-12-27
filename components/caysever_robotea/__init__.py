import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, switch, text_sensor, select
from esphome.const import CONF_ID, CONF_SENSOR

CODEOWNERS = ["@omerfaruk-aran"]
AUTO_LOAD = ["sensor", "switch", "select", "text_sensor"]

caysever_ns = cg.esphome_ns.namespace("caysever_robotea")
CayseverRobotea = caysever_ns.class_("CayseverRobotea", cg.Component)
CayseverRoboteaSwitch = caysever_ns.class_(
    "CayseverRoboteaSwitch", switch.Switch, cg.Component
)

CayseverRoboteaSelect = caysever_ns.class_(
    "CayseverRoboteaSelect", select.Select, cg.Component
)

CONF_ACTIVE_MODE_SENSOR = "active_mode_sensor"
CONF_CAY_DEMLEME = "cay_demleme"
CONF_SU_KAYNATMA = "su_kaynatma_switch"
# CONF_FILTRE_KAHVE = "filtre_kahve" //TODO
CONF_MAMA_SUYU = "mama_suyu_switch"

CAY_DEMLEME_LEVEL_OPTIONS = [
    "1/4",
    "2/4",
    "3/4",
    "MAX",
    "OFF",
]

SWITCH_SCHEMA = switch.SWITCH_SCHEMA.extend(cv.COMPONENT_SCHEMA).extend(
    {cv.GenerateID(): cv.declare_id(CayseverRoboteaSwitch)}
)

SELECT_SCHEMA = select.SELECT_SCHEMA.extend(
    {cv.GenerateID(CONF_ID): cv.declare_id(CayseverRoboteaSelect)}
)

TEXT_SENSOR_SCHEMA = text_sensor.TEXT_SENSOR_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(text_sensor.TextSensor),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CayseverRobotea),
        cv.Required(CONF_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_SU_KAYNATMA): SWITCH_SCHEMA,
        cv.Optional(CONF_MAMA_SUYU): SWITCH_SCHEMA,
        cv.Optional(CONF_CAY_DEMLEME): SELECT_SCHEMA,
        cv.Optional(CONF_ACTIVE_MODE_SENSOR): TEXT_SENSOR_SCHEMA,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    ntc_sensor = await cg.get_variable(config[CONF_SENSOR])

    cg.add(var.set_ntc_sensor(ntc_sensor))

    if CONF_CAY_DEMLEME in config:
        conf = config[CONF_CAY_DEMLEME]
        level_select = await select.new_select(conf, options=CAY_DEMLEME_LEVEL_OPTIONS)
        await cg.register_component(level_select, conf)
        cg.add(var.set_cay_demleme_select(level_select))
        
    for s in [
        CONF_SU_KAYNATMA,
        CONF_MAMA_SUYU,
    ]:
        if s in config:
            conf = config[s]
            a_switch = cg.new_Pvariable(conf[CONF_ID])
            await cg.register_component(a_switch, conf)
            await switch.register_switch(a_switch, conf)
            cg.add(getattr(var, f"set_{s}")(a_switch))

    if CONF_ACTIVE_MODE_SENSOR in config:
        sens_conf = config[CONF_ACTIVE_MODE_SENSOR]
        mode_sens = cg.new_Pvariable(sens_conf[CONF_ID])
        await text_sensor.register_text_sensor(mode_sens, sens_conf)
        cg.add(var.set_mode_sensor(mode_sens))
        
    await cg.register_component(var, config)
