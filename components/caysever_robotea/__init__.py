import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, switch, text_sensor, select
from esphome.const import (
    CONF_ID,
    CONF_SENSOR,
    CONF_ICON,
    UNIT_MINUTE,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

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
CONF_CAY_DEMLEME_MAX_SWITCH = "cay_demleme_max_switch"
CONF_SU_KAYNATMA = "su_kaynatma_switch"
# CONF_FILTRE_KAHVE = "filtre_kahve" //TODO
CONF_MAMA_SUYU = "mama_suyu_switch"
CONF_BUTON_SESI_SWITCH = "buton_sesi_switch"
CONF_KONUSMA_SESI_SWITCH = "konusma_sesi_switch"
CONF_SU_KONTROL_SWITCH = "su_kontrol_switch"
CONF_CAY_TAZELIK_SENSOR = "cay_tazelik_sensor"
CONF_CAY_TAZELIK_KALAN_SENSOR = "cay_tazelik_kalan_sensor"
CONF_SU_BITTI_ALGISI_SWITCH = "su_bitti_algisi_switch"
CONF_DEMLEME_HATTI_SENSOR = "demleme_hatti_sensor"
CONF_OTOMATIK_KAPANMA = "otomatik_kapanma"
CONF_MAMA_SUYU_SICAK_TUTMA = "mama_suyu_sicak_tutma"

CAY_DEMLEME_LEVEL_OPTIONS = [
    "1/4",
    "2/4",
    "3/4",
    "MAX",
    "KAPALI",
]

SWITCH_SCHEMA = switch.switch_schema(CayseverRoboteaSwitch).extend(cv.COMPONENT_SCHEMA)

SELECT_SCHEMA = select.select_schema(CayseverRoboteaSelect)

TEXT_SENSOR_SCHEMA = text_sensor.text_sensor_schema()

SU_KAYNATMA_SCHEMA = (
    switch.switch_schema(CayseverRoboteaSwitch)
    .extend(cv.COMPONENT_SCHEMA)
    .extend({cv.Optional(CONF_ICON, default="mdi:kettle"): cv.icon})
)

MAMA_SUYU_SCHEMA = (
    switch.switch_schema(CayseverRoboteaSwitch)
    .extend(cv.COMPONENT_SCHEMA)
    .extend({cv.Optional(CONF_ICON, default="mdi:baby-bottle-outline"): cv.icon})
)

CAY_DEMLEME_SCHEMA = select.select_schema(CayseverRoboteaSelect).extend(
    {cv.Optional(CONF_ICON, default="mdi:tea"): cv.icon}
)

CAY_DEMLEME_MAX_SWITCH_SCHEMA = (
    switch.switch_schema(CayseverRoboteaSwitch)
    .extend(cv.COMPONENT_SCHEMA)
    .extend({cv.Optional(CONF_ICON, default="mdi:tea"): cv.icon})
)

AKTIF_MOD_SCHEMA = text_sensor.text_sensor_schema().extend(
    {cv.Optional(CONF_ICON, default="mdi:toggle-switch-outline"): cv.icon}
)

MOD_DURUM_SCHEMA = text_sensor.text_sensor_schema().extend(
    {cv.Optional(CONF_ICON, default="mdi:information-outline"): cv.icon}
)

KETTLE_DURUM_SCHEMA = text_sensor.text_sensor_schema().extend(
    {cv.Optional(CONF_ICON, default="mdi:kettle"): cv.icon}
)

CAY_TAZELIK_SCHEMA = text_sensor.text_sensor_schema().extend(
    {cv.Optional(CONF_ICON, default="mdi:tea-outline"): cv.icon}
)

CAY_TAZELIK_KALAN_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_MINUTE,
    icon="mdi:timer-sand",
    accuracy_decimals=0,
)

# Tanılama: demleme hattı girişinde (GPIO34) saniyede görülen kenar sayısı
DEMLEME_HATTI_SCHEMA = sensor.sensor_schema(
    unit_of_measurement="kenar/sn",
    icon="mdi:sine-wave",
    accuracy_decimals=0,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CayseverRobotea),
        cv.Required(CONF_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_SU_KAYNATMA): SU_KAYNATMA_SCHEMA,
        cv.Optional(CONF_MAMA_SUYU): MAMA_SUYU_SCHEMA,
        cv.Optional(CONF_CAY_DEMLEME): CAY_DEMLEME_SCHEMA,
        cv.Optional(CONF_CAY_DEMLEME_MAX_SWITCH): CAY_DEMLEME_MAX_SWITCH_SCHEMA,
        cv.Optional(CONF_ACTIVE_MODE_SENSOR): AKTIF_MOD_SCHEMA,
        cv.Optional(CONF_MODE_STATE_SENSOR): MOD_DURUM_SCHEMA,
        cv.Optional(CONF_KETTLE_STATE_SENSOR): KETTLE_DURUM_SCHEMA,
        cv.Optional(CONF_CAY_TAZELIK_SENSOR): CAY_TAZELIK_SCHEMA,
        cv.Optional(CONF_CAY_TAZELIK_KALAN_SENSOR): CAY_TAZELIK_KALAN_SCHEMA,
        cv.Optional(CONF_BUTON_SESI_SWITCH): cv.use_id(switch.Switch),
        cv.Optional(CONF_KONUSMA_SESI_SWITCH): cv.use_id(switch.Switch),
        cv.Optional(CONF_SU_KONTROL_SWITCH): cv.use_id(switch.Switch),
        # Demlemeyi fabrika yazılımındaki gibi "su bitti" algısıyla bitir (GPIO34). Verilmezse eski, süreli düzen.
        cv.Optional(CONF_SU_BITTI_ALGISI_SWITCH): cv.use_id(switch.Switch),
        cv.Optional(CONF_DEMLEME_HATTI_SENSOR): DEMLEME_HATTI_SCHEMA,
        # Mod açıldıktan bu süre sonra cihaz kendini kapatır (fabrika yazılımında 2 saat). Verilmezse kapanmaz.
        cv.Optional(CONF_OTOMATIK_KAPANMA): cv.positive_time_period_milliseconds,
        # Mama suyu "hazır" olduktan bu süre sonra mod kapanır. Verilmezse yalnız otomatik_kapanma geçerlidir.
        cv.Optional(CONF_MAMA_SUYU_SICAK_TUTMA): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    # WiFi.onEvent için Arduino WiFi kütüphanesi (yeni ESPHome'da varsayılan kapalı)
    cg.add_library("WiFi", None)
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

    if CONF_SU_BITTI_ALGISI_SWITCH in config:
        su_bitti = await cg.get_variable(config[CONF_SU_BITTI_ALGISI_SWITCH])
        cg.add(var.set_su_bitti_algisi_switch(su_bitti))

    if CONF_DEMLEME_HATTI_SENSOR in config:
        hat_sens = await sensor.new_sensor(config[CONF_DEMLEME_HATTI_SENSOR])
        cg.add(var.set_demleme_hatti_sensor(hat_sens))

    if CONF_OTOMATIK_KAPANMA in config:
        cg.add(var.set_otomatik_kapanma(config[CONF_OTOMATIK_KAPANMA]))

    if CONF_MAMA_SUYU_SICAK_TUTMA in config:
        cg.add(var.set_mama_suyu_sicak_tutma(config[CONF_MAMA_SUYU_SICAK_TUTMA]))

    for s in [
        CONF_SU_KAYNATMA,
        CONF_MAMA_SUYU,
        CONF_CAY_DEMLEME_MAX_SWITCH,
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
        await text_sensor.register_text_sensor(
            kettle_state_sens, kettle_state_sens_conf
        )
        cg.add(var.set_kettle_state_sensor(kettle_state_sens))

    if CONF_CAY_TAZELIK_SENSOR in config:
        tazelik_conf = config[CONF_CAY_TAZELIK_SENSOR]
        tazelik_sens = cg.new_Pvariable(tazelik_conf[CONF_ID])
        await text_sensor.register_text_sensor(tazelik_sens, tazelik_conf)
        cg.add(var.set_tazelik_sensor(tazelik_sens))

    if CONF_CAY_TAZELIK_KALAN_SENSOR in config:
        tazelik_kalan_sens = await sensor.new_sensor(config[CONF_CAY_TAZELIK_KALAN_SENSOR])
        cg.add(var.set_tazelik_kalan_sensor(tazelik_kalan_sens))

    await cg.register_component(var, config)
