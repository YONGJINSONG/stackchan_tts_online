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
    "#if CONFIG_IDF_TARGET_ESP32\n"
    "    cam_obj->psram_mode = false;\n"
    "#else\n"
    "    cam_obj->psram_mode = (config->xclk_freq_hz == 16000000);\n"
    "#endif\n"
)
hal_psram_new = (
    "#if CONFIG_IDF_TARGET_ESP32\n"
    "    cam_obj->psram_mode = false;\n"
    "#else\n"
    "    // Keep 20 MHz XCLK. GC0308 PCLK is already /4, so RGB565 can DMA\n"
    "    // straight into PSRAM instead of memcpy from a 15 KB SRAM bounce\n"
    "    // buffer. That memcpy is what overflows the EOF queue while Wi-Fi\n"
    "    // holds the PSRAM bus.\n"
    "    cam_obj->psram_mode = true;\n"
    "#endif\n"
)
hal_text = open(camera_hal_source, "r", encoding="utf-8").read()
if hal_queue_old not in hal_text or hal_psram_old not in hal_text:
    print("[camera-dma] cam_hal.c queue or psram snippet not found")
    env.Exit(1)
hal_text = hal_text.replace(hal_queue_old, hal_queue_new, 1)
open(os.path.join(patched_hal_dir, "cam_hal.c"), "w", encoding="utf-8", newline="\n").write(
    hal_text.replace(hal_psram_old, hal_psram_new, 1)
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
    "        // CoreS3 keeps 20 MHz XCLK and a 16 KB DMA ceiling, so the first\n"
    "        // cam_start() must already be at XCLK/4 (~5 MHz PCLK).\n"
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
print("[camera-dma] patched cam_hal.c EOF queue/psram DMA and gc0308.c S3 PCLK")

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
        source_path.endswith("/esp32-camera/target/esp32s3/ll_cam.c")
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
    os.path.join(camera_source_root, "target", "esp32s3"),
    src_filter="+<ll_cam.c>",
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
