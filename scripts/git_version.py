import subprocess

Import("env")


def detect_version():
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--long", "--dirty", "--always"],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
        if out:
            return out
    except Exception:
        pass
    return "dev"


version = detect_version().replace('"', "")
env.Append(BUILD_FLAGS=[f'-DTHERMOSTAT_FIRMWARE_VERSION=\\"{version}\\"'])

