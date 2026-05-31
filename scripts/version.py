Import("env")
from datetime import datetime, timezone

base = env.GetProjectOption("custom_fw_version", "0.1.1")
stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M")
version = f"{base}+{stamp}"

env.Append(CPPDEFINES=[("NODE_FIRMWARE_VERSION", f'\\"{version}\\"')])
print(f"[version] NODE_FIRMWARE_VERSION = {version}")
