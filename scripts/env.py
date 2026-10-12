import os
import subprocess

Import("env")

STRING_KEYS = ("WIFI_SSID", "WIFI_PASS", "CAL_TOKEN", "CAL_HOST", "TZ_POSIX")
NUMBER_KEYS = ("CAL_PORT", "CAL_INSECURE", "GEO_LAT", "GEO_LON", "DISPLAY_ROTATION")


def read_dotenv(path):
    values = {}
    if not os.path.exists(path):
        return values
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip().removeprefix("export ").strip()
            value = value.strip()
            if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
                value = value[1:-1]
            values[key] = value
    return values


def ensure_fonts(project_dir):
    script = os.path.join(project_dir, "scripts", "fonts.py")
    out = os.path.join(project_dir, "src", "fonts_gen.h")
    if os.path.exists(out) and os.path.getmtime(out) >= os.path.getmtime(script):
        return
    python = env.subst("$PYTHONEXE")
    if subprocess.run([python, "-c", "import freetype"], capture_output=True).returncode != 0:
        subprocess.check_call([python, "-m", "pip", "install", "freetype-py"])
    subprocess.check_call([python, script, out, os.path.join(project_dir, ".fonts")])


project_dir = env.subst("$PROJECT_DIR")
values = {**read_dotenv(os.path.join(project_dir, ".env")), **{k: os.environ[k] for k in STRING_KEYS + NUMBER_KEYS if k in os.environ}}

for key in ("WIFI_SSID", "CAL_TOKEN"):
    if not values.get(key):
        raise SystemExit(f".env: {key} is required")

defines = []
for key in STRING_KEYS:
    if key in values:
        defines.append((key, env.StringifyMacro(values[key])))
for key in NUMBER_KEYS:
    if key in values:
        defines.append((key, values[key]))
env.Append(CPPDEFINES=defines)

ensure_fonts(project_dir)
