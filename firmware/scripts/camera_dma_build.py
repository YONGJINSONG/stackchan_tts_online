Import("env")

import os

camera_source_root = env.subst(
    "$PROJECT_LIBDEPS_DIR/$PIOENV/esp32-camera"
)
camera_gc0308_source = os.path.join(
    camera_source_root, "sensors", "gc0308.c"
)
if not os.path.isfile(camera_gc0308_source):
    print("[camera-dma] missing gc0308.c: " + camera_gc0308_source)
    env.Exit(1)

patched_root = env.subst("$BUILD_DIR/camera_src")
patched_sensor_dir = os.path.join(patched_root, "sensors")
os.makedirs(patched_sensor_dir, exist_ok=True)

gc_shadow_old = (
    "#define H8(v) ((v)>>8)\n"
    "#define L8(v) ((v)&0xff)\n"
)
gc_shadow_new = (
    "#define H8(v) ((v)>>8)\n"
    "#define L8(v) ((v)&0xff)\n"
    "\n"
    "// CoreS3 GC0308 register reads time out after returning the sensor ID\n"
    "// byte (0x9b). Keep the orientation register in software so camera init\n"
    "// never feeds that stale byte into a read-modify-write operation.\n"
    "static uint8_t core_s3_reg14_shadow = 0x11;\n"
)
gc_reset_old = (
    "static int reset(sensor_t *sensor)\n"
    "{\n"
    "    int ret = 0;\n"
)
gc_reset_new = (
    "static int reset(sensor_t *sensor)\n"
    "{\n"
    "    int ret = 0;\n"
    "    core_s3_reg14_shadow = 0x11;\n"
)
gc_pixformat_old = (
    "    case PIXFORMAT_RGB565:\n"
    "        write_reg(sensor->slv_addr, 0xfe, 0x00);\n"
    "        ret = set_reg_bits(sensor->slv_addr, 0x24, 0, 0x0f, 6);  //RGB565\n"
    "        break;\n"
    "\n"
    "    case PIXFORMAT_YUV422:\n"
    "        write_reg(sensor->slv_addr, 0xfe, 0x00);\n"
    "        ret = set_reg_bits(sensor->slv_addr, 0x24, 0, 0x0f, 2); //yuv422 Y Cb Y Cr\n"
    "        break;\n"
)
gc_pixformat_new = (
    "    case PIXFORMAT_RGB565:\n"
    "        ret = write_reg(sensor->slv_addr, 0xfe, 0x00);\n"
    "        if (ret == 0) {\n"
    "            ret = write_reg(sensor->slv_addr, 0x24, 0xa6);  // RGB565, preserve upper defaults\n"
    "        }\n"
    "        break;\n"
    "\n"
    "    case PIXFORMAT_YUV422:\n"
    "        ret = write_reg(sensor->slv_addr, 0xfe, 0x00);\n"
    "        if (ret == 0) {\n"
    "            ret = write_reg(sensor->slv_addr, 0x24, 0xa2);  // YCbYCr, preserve upper defaults\n"
    "        }\n"
    "        break;\n"
)
gc_subsample_old = (
    "    write_reg(sensor->slv_addr, 0xfe, 0x01);\n"
    "    set_reg_bits(sensor->slv_addr, 0x53, 7, 0x01, 1);\n"
    "    set_reg_bits(sensor->slv_addr, 0x55, 0, 0x01, 1);\n"
)
gc_subsample_new = (
    "    ret |= write_reg(sensor->slv_addr, 0xfe, 0x01);\n"
    "    ret |= write_reg(sensor->slv_addr, 0x53, 0x80);\n"
    "    ret |= write_reg(sensor->slv_addr, 0x55, 0x01);\n"
)
gc_framesize_return_old = (
    "    if (ret == 0) {\n"
    "        ESP_LOGD(TAG, \"Set framesize to: %ux%u\", w, h);\n"
    "    }\n"
    "    return 0;\n"
)
gc_framesize_return_new = (
    "    if (ret == 0) {\n"
    "        ESP_LOGD(TAG, \"Set framesize to: %ux%u\", w, h);\n"
    "    }\n"
    "    return ret;\n"
)
gc_orientation_old = (
    "static int set_hmirror(sensor_t *sensor, int enable)\n"
    "{\n"
    "    int ret = 0;\n"
    "    sensor->status.hmirror = enable;\n"
    "    ret = write_reg(sensor->slv_addr, 0xfe, 0x00);\n"
    "    ret |= set_reg_bits(sensor->slv_addr, 0x14, 0, 0x01, enable != 0);\n"
    "    if (ret == 0) {\n"
    "        ESP_LOGD(TAG, \"Set h-mirror to: %d\", enable);\n"
    "    }\n"
    "    return ret;\n"
    "}\n"
    "\n"
    "static int set_vflip(sensor_t *sensor, int enable)\n"
    "{\n"
    "    int ret = 0;\n"
    "    sensor->status.vflip = enable;\n"
    "    ret = write_reg(sensor->slv_addr, 0xfe, 0x00);\n"
    "    ret |= set_reg_bits(sensor->slv_addr, 0x14, 1, 0x01, enable != 0);\n"
    "    if (ret == 0) {\n"
    "        ESP_LOGD(TAG, \"Set v-flip to: %d\", enable);\n"
    "    }\n"
    "    return ret;\n"
    "}\n"
)
gc_orientation_new = (
    "static int set_hmirror(sensor_t *sensor, int enable)\n"
    "{\n"
    "    int ret = write_reg(sensor->slv_addr, 0xfe, 0x00);\n"
    "    if (enable) {\n"
    "        core_s3_reg14_shadow |= 0x01;\n"
    "    } else {\n"
    "        core_s3_reg14_shadow &= (uint8_t)~0x01;\n"
    "    }\n"
    "    ret |= write_reg(sensor->slv_addr, 0x14, core_s3_reg14_shadow);\n"
    "    if (ret == 0) {\n"
    "        sensor->status.hmirror = enable;\n"
    "        ESP_LOGD(TAG, \"Set h-mirror to: %d\", enable);\n"
    "    }\n"
    "    return ret;\n"
    "}\n"
    "\n"
    "static int set_vflip(sensor_t *sensor, int enable)\n"
    "{\n"
    "    int ret = write_reg(sensor->slv_addr, 0xfe, 0x00);\n"
    "    if (enable) {\n"
    "        core_s3_reg14_shadow |= 0x02;\n"
    "    } else {\n"
    "        core_s3_reg14_shadow &= (uint8_t)~0x02;\n"
    "    }\n"
    "    ret |= write_reg(sensor->slv_addr, 0x14, core_s3_reg14_shadow);\n"
    "    if (ret == 0) {\n"
    "        sensor->status.vflip = enable;\n"
    "        ESP_LOGD(TAG, \"Set v-flip to: %d\", enable);\n"
    "    }\n"
    "    return ret;\n"
    "}\n"
)
gc_status_old = (
    "    sensor->status.hmirror = check_reg_mask(sensor->slv_addr, 0x14, 0x01);\n"
    "    sensor->status.vflip = check_reg_mask(sensor->slv_addr, 0x14, 0x02);\n"
)
gc_status_new = (
    "    sensor->status.hmirror = (core_s3_reg14_shadow & 0x01) != 0;\n"
    "    sensor->status.vflip = (core_s3_reg14_shadow & 0x02) != 0;\n"
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
if (
    gc_shadow_old not in gc_text
    or gc_reset_old not in gc_text
    or gc_pixformat_old not in gc_text
    or gc_subsample_old not in gc_text
    or gc_framesize_return_old not in gc_text
    or gc_orientation_old not in gc_text
    or gc_status_old not in gc_text
    or gc_tag_old not in gc_text
):
    print("[camera-dma] gc0308.c CoreS3 patch snippet not found")
    env.Exit(1)
gc_text = gc_text.replace(gc_tag_old, gc_tag_new, 1)
open(os.path.join(patched_sensor_dir, "gc0308.c"), "w", encoding="utf-8", newline="\n").write(
    gc_text.replace(gc_shadow_old, gc_shadow_new, 1)
    .replace(gc_reset_old, gc_reset_new, 1)
    .replace(gc_pixformat_old, gc_pixformat_new, 1)
    .replace(gc_subsample_old, gc_subsample_new, 1)
    .replace(gc_framesize_return_old, gc_framesize_return_new, 1)
    .replace(gc_orientation_old, gc_orientation_new, 1)
    .replace(gc_status_old, gc_status_new, 1)
)

print("[camera] framework stock HAL/LL (DMA 30720) + write-only CoreS3 GC0308")

camera_private_includes = [
    os.path.join(camera_source_root, "driver", "include"),
    os.path.join(camera_source_root, "driver", "private_include"),
    os.path.join(camera_source_root, "conversions", "include"),
    os.path.join(camera_source_root, "conversions", "private_include"),
    os.path.join(camera_source_root, "sensors", "private_include"),
    os.path.join(camera_source_root, "target", "private_include"),
]
env.Append(CPPPATH=camera_private_includes)

# Override only the GC0308 sensor unit. The framework's precompiled camera
# archive supplies its tested S3 cam_hal and ll_cam objects unchanged. Its
# 30720-byte QVGA RGB565 bounce buffer is required on the physical CoreS3;
# forcing smaller LL buffers causes EV-EOF-OVF before a frame can complete.
env.BuildSources(
    env.subst("$BUILD_DIR/camera_sensor"),
    patched_sensor_dir,
    src_filter="+<gc0308.c>",
)
