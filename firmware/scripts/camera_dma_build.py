Import("env")

import os


camera_dma_header = env.subst("$PROJECT_DIR/include/CameraDmaConfig.h")
camera_source_root = env.subst(
    "$PROJECT_LIBDEPS_DIR/$PIOENV/esp32-camera"
)
camera_ll_source = os.path.join(
    camera_source_root, "target", "esp32s3", "ll_cam.c"
)
camera_hal_source = os.path.join(camera_source_root, "driver", "cam_hal.c")
camera_gc0308_source = os.path.join(
    camera_source_root, "sensors", "gc0308.c"
)
if not os.path.isfile(camera_ll_source) or not os.path.isfile(camera_hal_source):
    print("[camera-dma] missing esp32-camera 2.0.4 sources: " + camera_source_root)
    env.Exit(1)
if not os.path.isfile(camera_gc0308_source):
    print("[camera-dma] missing gc0308.c: " + camera_gc0308_source)
    env.Exit(1)

patched_root = env.subst("$BUILD_DIR/camera_src")
patched_hal_dir = os.path.join(patched_root, "driver")
patched_sensor_dir = os.path.join(patched_root, "sensors")
os.makedirs(patched_hal_dir, exist_ok=True)
os.makedirs(patched_sensor_dir, exist_ok=True)
patched_ll_dir = os.path.join(patched_root, "target", "esp32s3")
os.makedirs(patched_ll_dir, exist_ok=True)

hal_queue_old = (
    "    size_t queue_size = cam_obj->dma_half_buffer_cnt - 1;\n"
    "    if (queue_size == 0) {\n"
    "        queue_size = 1;\n"
    "    }\n"
)
hal_queue_new = (
    "    size_t queue_size = cam_obj->dma_half_buffer_cnt - 1;\n"
    "    if (queue_size < 8) {\n"
        "        queue_size = 8;\n"
    "    }\n"
)
hal_psram_old = (
    "    cam_obj->jpeg_mode = config->pixel_format == PIXFORMAT_JPEG;\n"
    "#if CONFIG_IDF_TARGET_ESP32\n"
    "    cam_obj->psram_mode = false;\n"
    "#else\n"
    "    cam_obj->psram_mode = (config->xclk_freq_hz == 16000000);\n"
    "#endif\n"
)
hal_ovf_old = (
    "        ll_cam_stop(cam);\n"
    "        cam->state = CAM_STATE_IDLE;\n"
    "        ESP_CAMERA_ETS_PRINTF(DRAM_STR(\"cam_hal: EV-%s-OVF\\r\\n\"), cam_event==CAM_IN_SUC_EOF_EVENT ? DRAM_STR(\"EOF\") : DRAM_STR(\"VSYNC\"));\n"
)
hal_ovf_new = (
    "        ll_cam_stop(cam);\n"
    "        cam->state = CAM_STATE_IDLE;\n"
    "        static uint8_t s_ovf_log;\n"
    "        if (s_ovf_log < 3) {\n"
    "            s_ovf_log++;\n"
    "            ESP_CAMERA_ETS_PRINTF(DRAM_STR(\"cam_hal: EV-%s-OVF\\r\\n\"), cam_event==CAM_IN_SUC_EOF_EVENT ? DRAM_STR(\"EOF\") : DRAM_STR(\"VSYNC\"));\n"
    "        }\n"
)
hal_psram_new = (
    "    cam_obj->jpeg_mode = config->pixel_format == PIXFORMAT_JPEG;\n"
    "#if CONFIG_IDF_TARGET_ESP32\n"
    "    cam_obj->psram_mode = false;\n"
    "#else\n"
    "    // CoreS3 GC0308 RGB565 stability fix.\n"
    "    // Do NOT DMA RGB565 directly into PSRAM.\n"
    "    // Use internal-RAM bounce buffers, then copy to PSRAM FB.\n"
    "    cam_obj->psram_mode = false;\n"
    "#endif\n"
)
hal_text = open(camera_hal_source, "r", encoding="utf-8").read()
if hal_queue_old not in hal_text or hal_psram_old not in hal_text or hal_ovf_old not in hal_text:
    print("[camera-dma] cam_hal.c queue, psram, or ovf snippet not found")
    env.Exit(1)
open(os.path.join(patched_hal_dir, "cam_hal.c"), "w", encoding="utf-8", newline="\n").write(
    hal_text.replace(hal_queue_old, hal_queue_new, 1)
    .replace(hal_psram_old, hal_psram_new, 1)
    .replace(hal_ovf_old, hal_ovf_new, 1)
)

gc_pclk_old = (
    "#ifdef CONFIG_IDF_TARGET_ESP32\n"
    "        set_reg_bits(sensor->slv_addr, 0x28, 4, 0x07, 1);  //frequency division for esp32, ensure pclk <= 15MHz\n"
    "#endif\n"
)
gc_pclk_new = (
    "#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)\n"
    "        write_reg(sensor->slv_addr, 0xfe, 0x00);\n"
    "#ifdef CONFIG_IDF_TARGET_ESP32S3\n"
    "        // CoreS3 keeps 20 MHz XCLK; PCLK /4 (~5 MHz) for bounce DMA.\n"
    "        write_reg(sensor->slv_addr, 0x28, 0x32);\n"
    "#else\n"
    "        set_reg_bits(sensor->slv_addr, 0x28, 4, 0x07, 1);  //frequency division for esp32, ensure pclk <= 15MHz\n"
    "#endif\n"
    "#endif\n"
)
gc_tag_old = (
    '#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)\n'
    '#include "esp32-hal-log.h"\n'
    '#else\n'
)
gc_tag_new = (
    '#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)\n'
    '#include "esp32-hal-log.h"\n'
    'static const char *TAG = "gc0308";\n'
    '#else\n'
)
gc_text = open(camera_gc0308_source, "r", encoding="utf-8").read()
if gc_pclk_old not in gc_text or gc_tag_old not in gc_text:
    print("[camera-dma] gc0308.c PCLK or TAG snippet not found")
    env.Exit(1)
gc_text = gc_text.replace(gc_tag_old, gc_tag_new, 1)
open(os.path.join(patched_sensor_dir, "gc0308.c"), "w", encoding="utf-8", newline="\n").write(
    gc_text.replace(gc_pclk_old, gc_pclk_new, 1)
)

ll_clk_old = (
    "    LCD_CAM.cam_ctrl.cam_clk_sel = 3;//Select Camera module source clock. 0: no clock. 1: APLL. 2: CLK160. 3: no clock.\n"
)
ll_clk_new = (
    "    LCD_CAM.cam_ctrl.cam_clk_sel = 2;// CLK160. 2.0.4 value 3 is documented as no clock.\n"
)
ll_text = open(camera_ll_source, "r", encoding="utf-8").read()
if ll_text.count(ll_clk_old) < 1:
    print("[camera-dma] ll_cam.c cam_clk_sel snippet not found")
    env.Exit(1)
# ll_cam_config (probe XCLK) and xclk_timer_conf both ship as sel=3 / no clock.
open(os.path.join(patched_ll_dir, "ll_cam.c"), "w", encoding="utf-8", newline="\n").write(
    ll_text.replace(ll_clk_old, ll_clk_new)
)
print("[camera-dma] patched bounce DMA, EOF queue 8, S3 PCLK /4, cam_clk_sel CLK160")

camera_private_includes = [
    os.path.join(camera_source_root, "driver", "include"),
    os.path.join(camera_source_root, "driver", "private_include"),
    os.path.join(camera_source_root, "conversions", "include"),
    os.path.join(camera_source_root, "conversions", "private_include"),
    os.path.join(camera_source_root, "sensors", "private_include"),
    os.path.join(camera_source_root, "target", "private_include"),
]
env.Append(CPPPATH=camera_private_includes)


def apply_camera_dma_config(build_env, node):
    """Force camera-only overrides into the source-built camera units."""
    source_path = node.srcnode().get_abspath().replace("\\", "/")
    if (
        source_path.endswith("/ll_cam.c")
        or source_path.endswith("/cam_hal.c")
    ):
        print("[camera-dma] applying camera config to " + os.path.basename(source_path))
        return build_env.Object(
            node,
            CCFLAGS=build_env["CCFLAGS"]
            + ["-mlongcalls", "-include", camera_dma_header],
        )

    return node


env.AddBuildMiddleware(apply_camera_dma_config)

# The Arduino-ESP32 package ships a precompiled libesp32-camera.a, whose
# sdkconfig value cannot be changed by project flags. Add the complete S3
# ll_cam.c unit to the main program explicitly. It satisfies all low-level
# camera symbols before the framework archive is scanned, so the archive keeps
# supplying its tested common driver/sensor code without pulling its 32 KiB
# ll_cam object.
env.BuildSources(
    env.subst("$BUILD_DIR/camera_dma"),
    patched_ll_dir,
    src_filter="+<ll_cam.c>",
)
# -include CameraDmaConfig.h is not a SCons dependency, so bump the object
# whenever the header changes.
env.Depends(
    env.subst("$BUILD_DIR/camera_dma/ll_cam.o"),
    camera_dma_header,
)
env.BuildSources(
    env.subst("$BUILD_DIR/camera_hal"),
    patched_hal_dir,
    src_filter="+<cam_hal.c>",
)
# Same archive-override trick for the GC0308 reset path so PCLK /4 is in
# place before esp_camera_init() enables VSYNC.
env.BuildSources(
    env.subst("$BUILD_DIR/camera_sensor"),
    patched_sensor_dir,
    src_filter="+<gc0308.c>",
)
