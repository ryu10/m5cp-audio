Import("env")
# os モジュールをインポート
import os

def copyto_usb(source, target, env):
    # ターゲットドライブパス
    usb_drive = "/Volumes/NO NAME/"  # macOS の例。M5Launcher で USB デバイスモードを起動するのこの名前でマウントされる
    file_name = "cprecorder.bin"  # コピーするファイル名
    # ${BUILD_DIR}/file_name を usb_drive にコピー（もしusb_driveが存在すれば）
    if os.path.exists(usb_drive):
        src = f"{env.subst('$BUILD_DIR')}/{file_name}"
        env.Execute(f"cp '{src}' '{usb_drive}'")
        print(f"Copied {file_name} to {usb_drive}")
    else:
        print(f"USB drive {usb_drive} not found. Skipping copy {file_name} .")

# ビルドの有無に関わらず毎回実行されるカスタムターゲットとして登録
# AlwaysBuild により up-to-date でもスキップされない
target = env.AlwaysBuild(
    env.Alias("copyusb", "$BUILD_DIR/${PROGNAME}.bin", copyto_usb)
)
# デフォルトターゲット（pio run 時）に含める
Default(target)

