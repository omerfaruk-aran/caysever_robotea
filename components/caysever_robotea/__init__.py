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

CONF_KETTLE_STATE_SENSOR = "kettle_state_sensor"
CONF_ACTIVE_MODE_SENSOR = "active_mode_sensor"
CONF_MODE_STATE_SENSOR = "mode_state_sensor"
CONF_CAY_DEMLEME = "cay_demleme"
CONF_SU_KAYNATMA = "su_kaynatma_switch"
# CONF_FILTRE_KAHVE = "filtre_kahve" //TODO
CONF_MAMA_SUYU = "mama_suyu_switch"
CONF_BUTON_SESI_SWITCH = "buton_sesi_switch"
CONF_KONUSMA_SESI_SWITCH = "konusma_sesi_switch"
CONF_SU_KONTROL_SWITCH = "su_kontrol_switch"

CAY_DEMLEME_LEVEL_OPTIONS = [
    "1/4",
    "2/4",
    "3/4",
    "MAX",
    "KAPALI",
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
        cv.Optional(CONF_MODE_STATE_SENSOR): TEXT_SENSOR_SCHEMA,
        cv.Optional(CONF_KETTLE_STATE_SENSOR): TEXT_SENSOR_SCHEMA,
        cv.Optional(CONF_BUTON_SESI_SWITCH): cv.use_id(switch.Switch),
        cv.Optional(CONF_KONUSMA_SESI_SWITCH): cv.use_id(switch.Switch),
        cv.Optional(CONF_SU_KONTROL_SWITCH): cv.use_id(switch.Switch),
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

    if CONF_BUTON_SESI_SWITCH in config:
        buton_sesi = await cg.get_variable(config[CONF_BUTON_SESI_SWITCH])
        cg.add(var.set_buton_sesi_switch(buton_sesi))
    
    if CONF_KONUSMA_SESI_SWITCH in config:
        konusma_sesi = await cg.get_variable(config[CONF_KONUSMA_SESI_SWITCH])
        cg.add(var.set_konusma_sesi_switch(konusma_sesi))
        
    if CONF_SU_KONTROL_SWITCH in config:
        su_kontrol = await cg.get_variable(config[CONF_SU_KONTROL_SWITCH])
        cg.add(var.set_su_kontrol_switch(su_kontrol))

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

    if CONF_MODE_STATE_SENSOR in config:
        sens_state_conf = config[CONF_MODE_STATE_SENSOR]
        mode_state_sens = cg.new_Pvariable(sens_state_conf[CONF_ID])
        await text_sensor.register_text_sensor(mode_state_sens, sens_state_conf)
        cg.add(var.set_mode_state_sensor(mode_state_sens))

    if CONF_KETTLE_STATE_SENSOR in config:
        kettle_state_sens_conf = config[CONF_KETTLE_STATE_SENSOR]
        kettle_state_sens = cg.new_Pvariable(kettle_state_sens_conf[CONF_ID])
        await text_sensor.register_text_sensor(kettle_state_sens, kettle_state_sens_conf)
        cg.add(var.set_kettle_state_sensor(kettle_state_sens))

    await cg.register_component(var, config)
