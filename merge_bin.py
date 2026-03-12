Import("env")


def merge_bin(source, target, env):
    # 出力ファイル名
    # merged_bin = "$BUILD_DIR/${PROGNAME}_merged.bin"
    merged_bin = "$BUILD_DIR/cprecorder.bin"
    
    # esptool を使用してマージを実行
    env.Execute(
        " ".join([
            "$PYTHONEXE", "$OBJCOPY", 
            "--chip", "esp32s3", # Cardputer-adv
            "merge_bin",
            "-o", merged_bin,
            "--flash_mode", "dio",
            "--flash_size", "8MB",  # Cardputer-Adv は 8MB
            "0x0000", "$BUILD_DIR/bootloader.bin",
            "0x8000", "$BUILD_DIR/partitions.bin",
            "0x10000", "$BUILD_DIR/firmware.bin"
        ])
    )
    print(f"Merged binary created: {merged_bin}")

# ビルド完了後に実行するよう登録
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)
