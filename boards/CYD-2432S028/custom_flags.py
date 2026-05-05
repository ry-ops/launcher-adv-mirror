import os
import re
import json
from SCons.Script import Import

Import("env")

# Acessa as configurações do ambiente atual
config = env.GetProjectConfig()
section = f"env:{env['PIOENV']}"

# Busca o valor de 'var_board' definido no platformio.ini
# O segundo argumento é o valor padrão caso não encontre
board = config.get(section, "custom_var_board", default=None)

# Caminho para o JSON correspondente à board selecionada
BOARD_JSON_PATH = os.path.join(env["PROJECT_DIR"], "boards","_jsonfiles", f"{board}.json")

print(f"\ncustom_var_board = {BOARD_JSON_PATH}\n")

def load_board_config():
    """Carrega as configurações da board a partir do JSON."""
    try:
        with open(BOARD_JSON_PATH, "r") as file:
            return json.load(file)
    except Exception as e:
        print(f"Erro ao carregar {BOARD_JSON_PATH}: {e}")
        return {}


def clean_flag_string(flag):
    if not isinstance(flag, str):
        return ""
    flag = flag.strip()
    if len(flag) >= 2 and ((flag[0] == "'" and flag[-1] == "'") or (flag[0] == '"' and flag[-1] == '"')):
        flag = flag[1:-1].strip()
    return flag


def build_defines_from_extra_flags(extra_flags):
    defines = {}
    for raw_flag in extra_flags or []:
        flag = clean_flag_string(raw_flag)
        if not flag or not flag.startswith("-D"):
            continue

        rest = flag[2:].strip()
        if not rest:
            continue

        if "=" in rest:
            name, value = rest.split("=", 1)
            name = name.strip()
            value = value.strip()
            if len(value) >= 2 and ((value[0] == "'" and value[-1] == "'") or (value[0] == '"' and value[-1] == '"')):
                value = value[1:-1]
            defines[name] = value
        else:
            defines[rest] = "1"
    return defines


def parse_build_src_flags(build_src_flags):
    """Parse build_src_flags string into a dict of defines."""
    if isinstance(build_src_flags, list):
        flags = [f.strip() for f in build_src_flags if f.strip()]
    elif isinstance(build_src_flags, str):
        flags = [f.strip() for f in build_src_flags.split() if f.strip()]
    else:
        flags = []
    return build_defines_from_extra_flags(flags)


def resolve_macro_value(raw_value, defines):
    if raw_value is None:
        return raw_value
    resolved = str(raw_value).strip()
    if resolved in defines:
        return defines[resolved]

    for _ in range(10):
        changed = False
        for name, value in defines.items():
            if value is None:
                continue
            new_resolved = re.sub(rf"\b{re.escape(name)}\b", value, resolved)
            if new_resolved != resolved:
                resolved = new_resolved
                changed = True
        if not changed:
            break

    return resolved


def generate_build_flags(board_config, existing_defines):
    """Gera as build_flags dinamicamente baseado no JSON da board, evitando sobrescrever defines existentes."""
    flags = []
    extra_flags = board_config.get("build", {}).get("extra_flags", [])
    build_defines = build_defines_from_extra_flags(extra_flags)
    resolve = lambda value: resolve_macro_value(value, build_defines)

    def add_flag_if_not_defined(flag_name, value):
        if flag_name not in existing_defines:
            flags.append(f"-D{flag_name}={value}")

    # Configurações de hardware
    add_flag_if_not_defined("HAS_BTN", "0")
    add_flag_if_not_defined("SEL_BTN", "-1")
    add_flag_if_not_defined("UP_BTN", "-1")
    add_flag_if_not_defined("DW_BTN", "-1")
    add_flag_if_not_defined("BTN_ACT", "LOW")
    add_flag_if_not_defined("BTN_ALIAS", '"Sel"')
    add_flag_if_not_defined("MINBRIGHT", "190")
    add_flag_if_not_defined("BACKLIGHT", "21")
    add_flag_if_not_defined("LED", "-1")
    add_flag_if_not_defined("LED_ON", "LOW")
    add_flag_if_not_defined("HAS_TOUCH", "1")

    # Configuração de brilho do display
    add_flag_if_not_defined("TFT_BRIGHT_CHANNEL", "0")
    add_flag_if_not_defined("TFT_BRIGHT_Bits", "8")
    add_flag_if_not_defined("TFT_BRIGHT_FREQ", "5000")

    # Verifica os drivers de video habilitados na board
    if any("DISPLAY_ILI9341_SPI" in flag for flag in extra_flags):
        add_flag_if_not_defined("ILI9341_DRIVER", "1")
        add_flag_if_not_defined("TFT_MISO", resolve('ILI9341_SPI_BUS_MISO_IO_NUM'))
        add_flag_if_not_defined("TFT_MOSI", resolve('ILI9341_SPI_BUS_MOSI_IO_NUM'))
        add_flag_if_not_defined("TFT_SCLK", resolve('ILI9341_SPI_BUS_SCLK_IO_NUM'))
        add_flag_if_not_defined("TFT_CS", resolve('ILI9341_SPI_CONFIG_CS_GPIO_NUM'))
        add_flag_if_not_defined("TFT_DC", resolve('ILI9341_SPI_CONFIG_DC_GPIO_NUM'))
        add_flag_if_not_defined("TFT_RST", resolve('ILI9341_DEV_CONFIG_RESET_GPIO_NUM'))
        add_flag_if_not_defined("TFT_BL", resolve('GPIO_BCKL'))
        add_flag_if_not_defined("TFT_WIDTH", resolve('DISPLAY_WIDTH'))
        add_flag_if_not_defined("TFT_HEIGHT", resolve('DISPLAY_HEIGHT'))
        add_flag_if_not_defined("TFT_IPS", "0")
        add_flag_if_not_defined("TFT_COL_OFS1", "0")
        add_flag_if_not_defined("TFT_ROW_OFS1", "0")
        add_flag_if_not_defined("TFT_COL_OFS2", "0")
        add_flag_if_not_defined("TFT_ROW_OFS2", "0")
        add_flag_if_not_defined("ROTATION", "0")

    elif any("DISPLAY_ST7796_SPI" in flag for flag in extra_flags):
        add_flag_if_not_defined("ST7796_DRIVER", "1")
        add_flag_if_not_defined("TFT_MISO", resolve('ST7796_SPI_BUS_MISO_IO_NUM'))
        add_flag_if_not_defined("TFT_MOSI", resolve('ST7796_SPI_BUS_MOSI_IO_NUM'))
        add_flag_if_not_defined("TFT_SCLK", resolve('ST7796_SPI_BUS_SCLK_IO_NUM'))
        add_flag_if_not_defined("TFT_CS", resolve('ST7796_SPI_CONFIG_CS_GPIO_NUM'))
        add_flag_if_not_defined("TFT_DC", resolve('ST7796_SPI_CONFIG_DC_GPIO_NUM'))
        add_flag_if_not_defined("TFT_RST", resolve('ST7796_DEV_CONFIG_RESET_GPIO_NUM'))
        add_flag_if_not_defined("TFT_BL", resolve('GPIO_BCKL'))
        add_flag_if_not_defined("TFT_WIDTH", resolve('DISPLAY_WIDTH'))
        add_flag_if_not_defined("TFT_HEIGHT", resolve('DISPLAY_HEIGHT'))
        add_flag_if_not_defined("TFT_IPS", "0")
        add_flag_if_not_defined("TFT_COL_OFS1", "0")
        add_flag_if_not_defined("TFT_ROW_OFS1", "0")
        add_flag_if_not_defined("TFT_COL_OFS2", "0")
        add_flag_if_not_defined("TFT_ROW_OFS2", "0")
        add_flag_if_not_defined("ROTATION", "0")

    elif any("DISPLAY_ST7789_SPI" in flag for flag in extra_flags):
        add_flag_if_not_defined("ST7789_DRIVER", "1")
        add_flag_if_not_defined("TFT_MISO", resolve('ST7789_SPI_BUS_MISO_IO_NUM'))
        add_flag_if_not_defined("TFT_MOSI", resolve('ST7789_SPI_BUS_MOSI_IO_NUM'))
        add_flag_if_not_defined("TFT_SCLK", resolve('ST7789_SPI_BUS_SCLK_IO_NUM'))
        add_flag_if_not_defined("TFT_CS", resolve('ST7789_SPI_CONFIG_CS_GPIO_NUM'))
        add_flag_if_not_defined("TFT_DC", resolve('ST7789_SPI_CONFIG_DC_GPIO_NUM'))
        add_flag_if_not_defined("TFT_RST", resolve('ST7789_DEV_CONFIG_RESET_GPIO_NUM'))
        add_flag_if_not_defined("TFT_BL", resolve('GPIO_BCKL'))
        add_flag_if_not_defined("TFT_WIDTH", resolve('DISPLAY_WIDTH'))
        add_flag_if_not_defined("TFT_HEIGHT", resolve('DISPLAY_HEIGHT'))
        add_flag_if_not_defined("TFT_IPS", "0")
        add_flag_if_not_defined("TFT_COL_OFS1", "0")
        add_flag_if_not_defined("TFT_ROW_OFS1", "0")
        add_flag_if_not_defined("TFT_COL_OFS2", "0")
        add_flag_if_not_defined("TFT_ROW_OFS2", "0")
        add_flag_if_not_defined("ROTATION", "0")

    elif any("DISPLAY_AXS15231B_QSPI" in flag for flag in extra_flags):
        add_flag_if_not_defined("AXS15231B_QSPI", "1")
        add_flag_if_not_defined("TFT_MISO", "-1")
        add_flag_if_not_defined("TFT_MOSI", "-1")
        add_flag_if_not_defined("TFT_D0", resolve('AXS15231B_SPI_BUS_DATA0'))
        add_flag_if_not_defined("TFT_D1", resolve('AXS15231B_SPI_BUS_DATA1'))
        add_flag_if_not_defined("TFT_D2", resolve('AXS15231B_SPI_BUS_DATA2'))
        add_flag_if_not_defined("TFT_D3", resolve('AXS15231B_SPI_BUS_DATA3'))
        add_flag_if_not_defined("TFT_SCLK", resolve('AXS15231B_SPI_BUS_PCLK'))
        add_flag_if_not_defined("TFT_CS", resolve('AXS15231B_SPI_CONFIG_CS'))
        add_flag_if_not_defined("TFT_DC", resolve('AXS15231B_SPI_CONFIG_DC'))
        add_flag_if_not_defined("TFT_RST", resolve('AXS15231B_DEV_CONFIG_RESET'))
        add_flag_if_not_defined("TFT_BL", resolve('GPIO_BCKL'))
        add_flag_if_not_defined("TFT_WIDTH", resolve('DISPLAY_WIDTH'))
        add_flag_if_not_defined("TFT_HEIGHT", resolve('DISPLAY_HEIGHT'))
        add_flag_if_not_defined("TFT_IPS", "0")
        add_flag_if_not_defined("TFT_COL_OFS1", "0")
        add_flag_if_not_defined("TFT_ROW_OFS1", "0")
        add_flag_if_not_defined("TFT_COL_OFS2", "0")
        add_flag_if_not_defined("TFT_ROW_OFS2", "0")
        add_flag_if_not_defined("ROTATION", "0")


    elif any("DISPLAY_ST7789_I80" in flag for flag in extra_flags):
        add_flag_if_not_defined("LOVYAN_BUS", "Bus_Parallel8")
        add_flag_if_not_defined("LOVYAN_PANEL", "Panel_ST7789")
        add_flag_if_not_defined("USE_LOVYANGFX", "1")
        add_flag_if_not_defined("TFT_ROTATION", "0")
        add_flag_if_not_defined("TFT_INVERSION_OFF", "")
        add_flag_if_not_defined("TFT_PARALLEL_8_BIT", "")
        add_flag_if_not_defined("TFT_WIDTH", resolve('DISPLAY_WIDTH'))
        add_flag_if_not_defined("TFT_HEIGHT", resolve('DISPLAY_HEIGHT'))
        add_flag_if_not_defined("TFT_CS", resolve('ST7789_IO_I80_CONFIG_CS_GPIO_NUM'))
        add_flag_if_not_defined("TFT_DC", resolve('ST7789_I80_BUS_CONFIG_DC'))
        add_flag_if_not_defined("TFT_RST", resolve('ST7789_DEV_CONFIG_RESET_GPIO_NUM'))
        add_flag_if_not_defined("TFT_WR", resolve('ST7789_I80_BUS_CONFIG_WR'))
        add_flag_if_not_defined("TFT_RD", resolve('ST7789_RD_GPIO'))
        add_flag_if_not_defined("TFT_D0", resolve('ST7789_I80_BUS_CONFIG_DATA_GPIO_D8'))
        add_flag_if_not_defined("TFT_D1", resolve('ST7789_I80_BUS_CONFIG_DATA_GPIO_D9'))
        add_flag_if_not_defined("TFT_D2", resolve('ST7789_I80_BUS_CONFIG_DATA_GPIO_D10'))
        add_flag_if_not_defined("TFT_D3", resolve('ST7789_I80_BUS_CONFIG_DATA_GPIO_D11'))
        add_flag_if_not_defined("TFT_D4", resolve('ST7789_I80_BUS_CONFIG_DATA_GPIO_D12'))
        add_flag_if_not_defined("TFT_D5", resolve('ST7789_I80_BUS_CONFIG_DATA_GPIO_D13'))
        add_flag_if_not_defined("TFT_D6", resolve('ST7789_I80_BUS_CONFIG_DATA_GPIO_D14'))
        add_flag_if_not_defined("TFT_D7", resolve('ST7789_I80_BUS_CONFIG_DATA_GPIO_D15'))
        add_flag_if_not_defined("TFT_BCKL", resolve('GPIO_BCKL'))
        add_flag_if_not_defined("TFT_BL", resolve('GPIO_BCKL'))
        add_flag_if_not_defined("TFT_BUS_SHARED", "0")
        add_flag_if_not_defined("TFT_INVERTED", "0")
        add_flag_if_not_defined("GFX_BL", resolve('GPIO_BCKL'))
        add_flag_if_not_defined("TFT_IPS", "0")
        add_flag_if_not_defined("TFT_COL_OFS1", "0")
        add_flag_if_not_defined("TFT_ROW_OFS1", "0")
        add_flag_if_not_defined("ROTATION", "0")

    elif any("DISPLAY_ST7262_PAR" in flag for flag in extra_flags):
        add_flag_if_not_defined("RGB_PANEL", "1")
        add_flag_if_not_defined("TFT_BL", resolve('GPIO_BCKL'))
        add_flag_if_not_defined("TFT_WIDTH", resolve('DISPLAY_WIDTH'))
        add_flag_if_not_defined("TFT_HEIGHT", resolve('DISPLAY_HEIGHT'))
        add_flag_if_not_defined("ROTATION", "0")

    elif any("DISPLAY_ST7701_PAR" in flag for flag in extra_flags):
        add_flag_if_not_defined("RGB_PANEL", "1")
        add_flag_if_not_defined("TFT_BL", resolve('GPIO_BCKL'))
        add_flag_if_not_defined("TFT_WIDTH", resolve('DISPLAY_WIDTH'))
        add_flag_if_not_defined("TFT_HEIGHT", resolve('DISPLAY_HEIGHT'))
        add_flag_if_not_defined("ROTATION", "0")


    else:
        add_flag_if_not_defined("ILI9341_DRIVER", "1")
        add_flag_if_not_defined("TFT_MISO", "12")
        add_flag_if_not_defined("TFT_MOSI", "13")
        add_flag_if_not_defined("TFT_SCLK", "14")
        add_flag_if_not_defined("TFT_CS", "15")
        add_flag_if_not_defined("TFT_DC", "2")
        add_flag_if_not_defined("TFT_RST", "-1")
        add_flag_if_not_defined("TFT_BL", "21")
        add_flag_if_not_defined("TFT_WIDTH", "240")
        add_flag_if_not_defined("TFT_HEIGHT", "320")
        add_flag_if_not_defined("TFT_IPS", "0")
        add_flag_if_not_defined("TFT_COL_OFS1", "0")
        add_flag_if_not_defined("TFT_ROW_OFS1", "0")
        add_flag_if_not_defined("TFT_COL_OFS2", "0")
        add_flag_if_not_defined("TFT_ROW_OFS2", "0")
        add_flag_if_not_defined("ROTATION", "0")

    # Verifica suporte ao touch
    if any("TOUCH_XPT2046_SPI" in flag for flag in extra_flags):
        add_flag_if_not_defined("HAS_RESISTIVE_TOUCH", "1")
        add_flag_if_not_defined("CYD28_TouchR_IRQ", resolve('XPT2046_TOUCH_CONFIG_INT_GPIO_NUM'))
        add_flag_if_not_defined("CYD28_TouchR_MISO", resolve('XPT2046_SPI_BUS_MISO_IO_NUM'))
        add_flag_if_not_defined("CYD28_TouchR_MOSI", resolve('XPT2046_SPI_BUS_MOSI_IO_NUM'))
        add_flag_if_not_defined("CYD28_TouchR_CLK", resolve('XPT2046_SPI_BUS_SCLK_IO_NUM'))
        add_flag_if_not_defined("CYD28_TouchR_CS", resolve('XPT2046_SPI_CONFIG_CS_GPIO_NUM'))

    # Verifica suporte a cartão SD
    if any("BOARD_HAS_TF" in flag for flag in extra_flags):
        add_flag_if_not_defined("SDCARD_CS", resolve('TF_CS'))
        add_flag_if_not_defined("SDCARD_SCK", resolve('TF_SPI_SCLK'))
        add_flag_if_not_defined("SDCARD_MISO", resolve('TF_SPI_MISO'))
        add_flag_if_not_defined("SDCARD_MOSI", resolve('TF_SPI_MOSI'))
    else:
        add_flag_if_not_defined("SDCARD_CS", "5")
        add_flag_if_not_defined("SDCARD_SCK", "18")
        add_flag_if_not_defined("SDCARD_MISO", "19")
        add_flag_if_not_defined("SDCARD_MOSI", "23")

    return flags

# Carregar configurações do JSON da board correspondente
board_config = load_board_config()

# Obter build_src_flags existentes do platformio.ini
existing_build_src_flags = env.GetProjectOption('build_src_flags', default='')
existing_defines = parse_build_src_flags(existing_build_src_flags)

# Gerar as build_flags dinamicamente
build_flags = generate_build_flags(board_config, existing_defines)

# Adicionar as build_flags ao ambiente do PlatformIO
print("Adicionando build_flags dinâmicas:", build_flags)
env.Append(SRC_BUILD_FLAGS=build_flags)
